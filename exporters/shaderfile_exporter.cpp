
#include "shaderfile_exporter.h"

#include "compat/resource_loader_compat.h"
#include "core/error/error_list.h"
#include "core/io/file_access.h"
#include "core/object/property_info.h"
#include "core/variant/variant.h"
#include "spirv-interop/spirv-transpiler.h"
#include "utility/common.h"
#include "utility/gdre_settings.h"

String ShaderFileExporter::get_name() const {
	return EXPORTER_NAME;
}

Error ShaderFileExporter::export_file(const String &out_path, const String &res_path) {
	using namespace spirv_interop;
	auto res = ResourceCompatLoader::fake_load(res_path);
	ERR_FAIL_COND_V_MSG(res.is_null(), ERR_FILE_CANT_OPEN, "Failed to load resource: " + res_path);
	bool is_valid = false;

	// [gd_resource type="RDShaderFile" format=4 uid="uid://b1eqh7s615yny"]

	// [sub_resource type="RDShaderSPIRV" id="RDShaderSPIRV_sqve6"]
	// bytecode_compute = PackedByteArray("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")

	// [resource]
	// _versions = {
	// &"": SubResource("RDShaderSPIRV_sqve6")
	// }

	Dictionary versions = res->get("_versions", &is_valid);
	ERR_FAIL_COND_V_MSG(!is_valid || versions.is_empty(), ERR_INVALID_DATA, "Invalid versions: " + res_path);

	SPIRVCrossOptions args;

	Ref<ResourceInfo> info = ResourceInfo::get_info_from_resource(res);
	ERR_FAIL_COND_V_MSG(info.is_null(), ERR_INVALID_DATA, "Invalid resource info: " + res_path);

	// TODO: figure out if we need this or not
	args.vulkan_semantics = false;
	if (info->get_ver_major() >= 4 && GDRESettings::get_singleton()->is_project_config_loaded() && GDRESettings::get_singleton()->has_project_setting("application/config/features")) {
		PackedStringArray features = GDRESettings::get_singleton()->get_project_setting(res_path, "application/config/features");
		if (!features.has("GL Compatibility")) {
			args.vulkan_semantics = true;
			args.glsl_emit_ubo_as_plain_uniforms = false;
		} else {
			args.vulkan_semantics = false;
		}
	}

	String glsl_version_define = "";
	HashMap<String, HashMap<String, String>> output_map;
	for (const KeyValue<Variant, Variant> &kv : versions) {
		StringName vname = kv.key;
		Ref<Resource> bc = kv.value;
		ERR_CONTINUE(bc.is_null());
		List<PropertyInfo> properties;
		bc->get_property_list(&properties);
		Vector<String> bytecode_names;
		for (const PropertyInfo &pi : properties) {
			if (pi.name.begins_with("bytecode_")) {
				bytecode_names.push_back(pi.name);
			}
		}
		HashMap<String, String> version_output_map;
		for (const String &name : bytecode_names) {
			PackedByteArray bytecode = bc->get(name, &is_valid);
			ERR_CONTINUE(!is_valid);
			uint32_t *ptr = reinterpret_cast<uint32_t *>(bytecode.ptrw());
			size_t len = bytecode.size() / sizeof(uint32_t);
			std::vector<uint32_t> spirv_file(ptr, ptr + len);

			std::string _execution_model_str;
			std::string _error_message;
			auto result = compile_iteration(args, spirv_file, _execution_model_str, _error_message);
			if (!result.has_value()) {
				ERR_PRINT("Error compiling shader: " + String::utf8(_error_message.c_str(), _error_message.size()));
				return ERR_COMPILATION_FAILED;
			}
			auto out = String::utf8(result->c_str(), result->size());
			String execution_model_str = String::utf8(_execution_model_str.c_str(), _execution_model_str.size());
			String first_line = out.get_slice("\n", 0);
			if (glsl_version_define.is_empty()) {
				glsl_version_define = first_line;
			} else if (glsl_version_define != first_line) {
				WARN_PRINT("GSL version define mismatch: " + glsl_version_define + " != " + first_line);
			}
			out = out.trim_prefix(first_line);
			version_output_map[execution_model_str] = out;
		}
		output_map[vname] = version_output_map;
	}
	String final_output;

	Error ret_err = OK;
	if (output_map.size() == 1) {
		for (auto &entry : output_map.begin()->value) {
			final_output = "#[" + entry.key + "]\n";
			final_output += glsl_version_define + "\n\n";
			final_output += entry.value + "\n";
		}
	} else {
		// version header goes like this:
		// #[versions]

		// version_float = "#define VER_FLOAT";
		// version_half = "#define VER_HALF";
		// version_unorm8 = "#define VER_UINT8";
		// version_unorm16 = "#define VER_UINT16";
		const char *version_define_prefix = "VER_";
		String version_header = "#[versions]\n\n";

		for (const auto &kv : output_map) {
			version_header += vformat("%s = \"#define %s%s\";\n", kv.key, version_define_prefix, kv.key.to_upper());
		}

		final_output = version_header + "\n";

		// TODO: more advanced diffing here; right now we just create big if-else blocks for each version, which is not very efficient
		HashMap<String, Vector<Pair<String, String>>> reversed_output_map;

		// we have to reverse the ordering of the output_map; instead of version -> stage -> code, we want stage -> version -> code
		for (const auto &[version, stage_code_map] : output_map) {
			for (const auto &entry : stage_code_map) {
				reversed_output_map[entry.key].push_back({ version, entry.value });
			}
		}

		for (const auto &[stage, version_code_map] : reversed_output_map) {
			final_output += "#[" + stage + "]\n";
			final_output += glsl_version_define + "\n\n";
			final_output += "#VERSION_DEFINES\n\n";
			final_output += vformat("#if defined(%s%s)\n", version_define_prefix, version_code_map[0].first.to_upper());
			for (int i = 0; i < version_code_map.size() - 1; i++) {
				const auto &[version, code] = version_code_map[i];
				final_output += code + "\n";
				if (i + 1 < version_code_map.size() - 1) {
					final_output += vformat("#elif defined(%s%s)\n", version_define_prefix, version_code_map[i + 1].first.to_upper());
				}
			}
			final_output += vformat("#else // %s%s\n", version_define_prefix, version_code_map[version_code_map.size() - 1].first.to_upper());
			final_output += version_code_map[version_code_map.size() - 1].second + "\n";
			final_output += "#endif\n";
		}
	}

	gdre::ensure_dir(out_path.get_base_dir());
	Ref<FileAccess> fa = FileAccess::open(out_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(fa.is_null(), ERR_FILE_CANT_OPEN, "Failed to open file: " + out_path);
	if (!fa->store_string(final_output)) {
		return ERR_FILE_CANT_WRITE;
	}
	return ret_err;
}

Ref<ExportReport> ShaderFileExporter::export_resource(const String &output_dir, Ref<ImportInfo> import_infos) {
	Ref<ExportReport> report = Ref<ExportReport>(memnew(ExportReport(import_infos)));
	String dest = output_dir.path_join(import_infos->get_export_dest().trim_prefix("res://"));
	report->set_error(export_file(dest, import_infos->get_path()));
	report->set_resources_used({ import_infos->get_path() });
	if (report->get_error() == OK || report->get_error() == ERR_PRINTER_ON_FIRE) {
		report->set_saved_path(dest);
	}
	// no options
	return report;
}

void ShaderFileExporter::get_handled_types(List<String> *out) const {
	out->push_back("RDShaderFile");
}

void ShaderFileExporter::get_handled_importers(List<String> *out) const {
	out->push_back("glsl");
}

String ShaderFileExporter::get_default_export_extension(const String &res_path) const {
	return "glsl";
}

Vector<String> ShaderFileExporter::get_export_extensions(const String &res_path) const {
	Vector<String> extensions;
	extensions.push_back("glsl");
	return extensions;
}
