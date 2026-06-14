#include "test_bytecode.h"

#include "bytecode/bytecode_base.h"
#include "bytecode/bytecode_versions.h"
#include "core/object/property_info.h"
#include "core/version.h"
#include "modules/gdscript/tests/gdscript_test_runner.h"
#include "modules/gdscript/gdscript_cache.h"
#include "utility/gdre_settings.h"
#include "utility/gdre_logger.h"
namespace TestBytecode {
void test_individual_script(const String &script_path, const String &helper_script_text) {
	// disable this for now
	CoreGlobals::print_error_enabled = false;
	Ref<FakeGDScript> fake_script = memnew(FakeGDScript);
	fake_script->set_override_bytecode_revision(GDScriptDecompVersion::LATEST_GDSCRIPT_COMMIT);
	fake_script->load_source_code(script_path);
	Error err = fake_script->reload(false);
	CHECK(err == OK);

	if (script_path.contains("enum_access_types.gd")) {
		bool thing = false;
	}
	auto cwd = GDRESettings::get_singleton()->get_cwd();

	String local_path = cwd.path_join(script_path);//ProjectSettings::get_singleton()->localize_path(script_path);

	Ref<GDScript> real_script = memnew(GDScript);
	real_script->set_path(local_path);
	err = real_script->load_source_code(local_path);
	CHECK(err == OK);
	err = real_script->reload(false);
	// TODO: Figure out why the real loader fails to load some scripts
	if (err != OK){
		CoreGlobals::print_error_enabled = true;
		return;
	}
	CHECK(err == OK);
	List<PropertyInfo> decompiled_list;
	fake_script->get_script_property_list(&decompiled_list);
	// Vector<PropertyInfo> decompiled_vector = list_to_vector(decompiled_list);
	HashMap<String, PropertyInfo> decompiled_map;
	for (auto &E : decompiled_list) {
		decompiled_map[E.name] = E;
	}
	List<PropertyInfo> real_list;
	real_script->get_script_property_list(&real_list);
	Vector<PropertyInfo> real_vector = list_to_vector(real_list);

	for (int i = 0; i < real_vector.size(); i++) {
		if (((real_vector[i].usage & PropertyUsageFlags::PROPERTY_USAGE_STORAGE) == 0) || real_vector[i].usage & PROPERTY_USAGE_GROUP || real_vector[i].usage & PROPERTY_USAGE_CATEGORY || real_vector[i].usage & PROPERTY_USAGE_SUBGROUP) {
			continue;
		}
		CHECK(decompiled_map.has(real_vector[i].name));
	}
	CoreGlobals::print_error_enabled = true;
}

static bool generate_class_index_recursive(const String &p_dir) {
	Error err = OK;
	Ref<DirAccess> dir(DirAccess::open(p_dir, &err));

	if (err != OK) {
		return false;
	}

	String current_dir = dir->get_current_dir();

	dir->list_dir_begin();
	String next = dir->get_next();

	StringName gdscript_name = GDScriptLanguage::get_singleton()->get_name();
	while (!next.is_empty()) {
		if (dir->current_is_dir()) {
			if (next == "." || next == ".." || next == "completion" || next == "lsp") {
				next = dir->get_next();
				continue;
			}
			if (!generate_class_index_recursive(current_dir.path_join(next))) {
				return false;
			}
		} else {
			if (!next.ends_with(".gd")) {
				next = dir->get_next();
				continue;
			}
			String base_type;
			String source_file = current_dir.path_join(next);
			bool is_abstract = false;
			bool is_tool = false;
			String class_name = GDScriptLanguage::get_singleton()->get_global_class_name(source_file, &base_type, nullptr, &is_abstract, &is_tool);
			if (class_name.is_empty()) {
				next = dir->get_next();
				continue;
			}
			ERR_FAIL_COND_V_MSG(ScriptServer::is_global_class(class_name), false,
					"Class name '" + class_name + "' from " + source_file + " is already used in " + ScriptServer::get_global_class_path(class_name));

			ScriptServer::add_global_class(class_name, base_type, gdscript_name, source_file, is_abstract, is_tool);
		}

		next = dir->get_next();
	}

	dir->list_dir_end();

	return true;
}

bool generate_class_index(const String &source_dir) {
	Error err = OK;
	Ref<DirAccess> dir(DirAccess::open(source_dir, &err));

	ERR_FAIL_COND_V_MSG(err != OK, false, "Could not open specified test directory.");

	return generate_class_index_recursive(dir->get_current_dir());
}
void test_fake_script() {
	GDScriptTests::GDScriptTestRunner runner(get_gdscript_tests_path(), true, false, false);
	generate_class_index(get_gdscript_tests_path());
	GDRESettings::get_singleton()->_set_version_override(GODOT_VERSION_FULL_CONFIG);
	String project_path = GDRESettings::get_singleton()->get_project_path();
	GDRESettings::get_singleton()->set_project_path(get_gdscript_tests_path());
	Error err = GDRESettings::get_singleton()->load_project({get_gdscript_tests_path()});
	CHECK(err == OK);
	auto gdscript_test_scripts = get_gdscript2_0_scripts();
	for (int i = 0; i < gdscript_test_scripts.size(); i++) {
		auto script_path = gdscript_test_scripts[i];
		auto sub_case_name = vformat("Testing compiling script %s", script_path);
		String script_text = FileAccess::get_file_as_string(script_path);
		test_individual_script(script_path, script_text);
	}
	GDRESettings::get_singleton()->unload_project();
	GDRESettings::get_singleton()->_set_version_override("");
	GDRESettings::get_singleton()->set_project_path(project_path);
}

Vector<String> get_gdscript2_0_scripts() {
	REQUIRE(GDRESettings::get_singleton());
	auto cwd = GDRESettings::get_singleton()->get_cwd();
	String gdscript_tests_path = get_gdscript_tests_path();
	auto gdscript_test_scripts = Glob::rglob(gdscript_tests_path.path_join("**/*.gd"), true);
	auto gdscript_test_error_scripts = Vector<String>();
	for (int i = 0; i < gdscript_test_scripts.size(); i++) {
		// remove any that contain ".notest." or "/error/"
		auto script_path = gdscript_test_scripts[i].trim_prefix(cwd + "/");
		if (script_path.contains(".notest.") || script_path.contains("error") || script_path.contains("completion")) {
			gdscript_test_error_scripts.push_back(script_path);
			gdscript_test_scripts.erase(gdscript_test_scripts[i]);
			i--;
		} else {
			gdscript_test_scripts.write[i] = script_path;
		}
	}
	return gdscript_test_scripts;
}
} // namespace TestBytecode
