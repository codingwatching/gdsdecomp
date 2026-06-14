#include "thirdparty/spirv-cross/spirv_cross.hpp"
#include "thirdparty/spirv-cross/spirv_glsl.hpp"
#include "thirdparty/spirv-cross/spirv_parser.hpp"

#include "shaderfile_exporter.h"
#include "compat/resource_loader_compat.h"
#include "core/error/error_list.h"
#include "core/io/file_access.h"
#include "core/object/property_info.h"
#include "core/variant/variant.h"

#include "utility/common.h"
#include "utility/gdre_settings.h"

struct VariableTypeRemap {
	std::string variable_name;
	std::string new_variable_type;
};

struct SPIRVCrossOptions {
	const char *input = nullptr;
	const char *output = nullptr;
	const char *cpp_interface_name = nullptr;
	uint32_t version = 0;
	uint32_t shader_model = 0;
	bool es = false;
	bool set_version = false;
	bool set_shader_model = false;
	bool set_es = false;
	bool dump_resources = false;
	bool force_temporary = false;
	bool flatten_ubo = false;
	bool fixup = false;
	bool yflip = false;
	bool sso = false;
	bool support_nonzero_baseinstance = true;
	bool glsl_emit_push_constant_as_ubo = false;
	bool glsl_emit_ubo_as_plain_uniforms = false;
	bool glsl_force_flattened_io_blocks = false;
	uint32_t glsl_ovr_multiview_view_count = 0;
	std::vector<std::pair<uint32_t, uint32_t>> glsl_ext_framebuffer_fetch;
	bool glsl_ext_framebuffer_fetch_noncoherent = false;
	uint32_t glsl_descriptor_heap_set = UINT32_MAX;
	uint32_t glsl_descriptor_heap_binding = UINT32_MAX;
	bool vulkan_glsl_disable_ext_samplerless_texture_functions = false;
	bool emit_line_directives = false;
	bool enable_storage_image_qualifier_deduction = true;
	bool force_zero_initialized_variables = false;
	bool relax_nan_checks = false;
	uint32_t force_recompile_max_debug_iterations = 3;
	std::vector<std::string> extensions;
	std::vector<VariableTypeRemap> variable_type_remaps;
	std::vector<std::pair<uint32_t, uint32_t>> masked_stage_outputs;
	std::string entry;
	std::string entry_stage;

	uint32_t iterations = 1;
	bool cpp = false;
	std::string reflect;
	bool vulkan_semantics = false;
	bool flatten_multidimensional_arrays = false;
	bool use_420pack_extension = true;
	bool remove_unused = false;
	bool combined_samplers_inherit_bindings = false;
};

static String compile_iteration(const SPIRVCrossOptions &args, std::vector<uint32_t> spirv_file, String &execution_model_str);

String ShaderFileExporter::get_name() const {
	return EXPORTER_NAME;
}

Error ShaderFileExporter::export_file(const String &out_path, const String &res_path) {
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
			try {
				String execution_model_str;
				String out = compile_iteration(args, spirv_file, execution_model_str);
				String first_line = out.get_slice("\n", 0);
				if (glsl_version_define.is_empty()) {
					glsl_version_define = first_line;
				} else if (glsl_version_define != first_line) {
					WARN_PRINT("GSL version define mismatch: " + glsl_version_define + " != " + first_line);
				}
				out = out.trim_prefix(first_line);
				version_output_map[execution_model_str] = out;
			} catch (const std::exception &e) {
				ERR_PRINT("Error compiling shader: " + String::utf8(e.what()));
				return ERR_COMPILATION_FAILED;
			}
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

static const char *execution_model_to_str(spirv_cross::ExecutionModel model) {
	switch (model) {
		case spirv_cross::ExecutionModelVertex:
			return "vertex";
		case spirv_cross::ExecutionModelTessellationControl:
			return "tesselation_controlcontrol";
		case spirv_cross::ExecutionModelTessellationEvaluation:
			return "tesselation_evaluation";
		case spirv_cross::ExecutionModelGeometry:
			return "geometry";
		case spirv_cross::ExecutionModelFragment:
			return "fragment";
		case spirv_cross::ExecutionModelGLCompute:
			return "compute";
		case spirv_cross::ExecutionModelRayGenerationNV:
			return "raygen";
		case spirv_cross::ExecutionModelIntersectionNV:
			return "intersection";
		case spirv_cross::ExecutionModelCallableNV:
			return "callable";
		case spirv_cross::ExecutionModelAnyHitNV:
			return "anyhit";
		case spirv_cross::ExecutionModelClosestHitNV:
			return "closesthit";
		case spirv_cross::ExecutionModelMissNV:
			return "miss";
		default:
			return "???";
	}
}

static String compile_iteration(const SPIRVCrossOptions &args, std::vector<uint32_t> spirv_file, String &execution_model_str) {
	spirv_cross::Parser spirv_parser(std::move(spirv_file));
	spirv_parser.parse();

	std::unique_ptr<spirv_cross::CompilerGLSL> compiler;
	bool combined_image_samplers = false;
	bool build_dummy_sampler = false;

	{
		combined_image_samplers = !args.vulkan_semantics;
		if (!args.vulkan_semantics || args.vulkan_glsl_disable_ext_samplerless_texture_functions) {
			build_dummy_sampler = true;
		}
		compiler.reset(new spirv_cross::CompilerGLSL(std::move(spirv_parser.get_parsed_ir())));
	}

	if (!args.variable_type_remaps.empty()) {
		auto remap_cb = [&](const spirv_cross::SPIRType &, const std::string &name, std::string &out) -> void {
			for (const VariableTypeRemap &remap : args.variable_type_remaps) {
				if (name == remap.variable_name) {
					out = remap.new_variable_type;
				}
			}
		};

		compiler->set_variable_type_remap_callback(std::move(remap_cb));
	}

	for (auto &masked : args.masked_stage_outputs) {
		compiler->mask_stage_output_by_location(masked.first, masked.second);
	}
	// for (auto &masked : args.masked_stage_builtins)
	// 	compiler->mask_stage_output_by_builtin(masked);

	// for (auto &rename : args.entry_point_rename)
	// 	compiler->rename_entry_point(rename.old_name, rename.new_name, rename.execution_model);

	auto entry_points = compiler->get_entry_points_and_stages();
	auto entry_point = args.entry;
	// ExecutionModel model = ExecutionModelMax;

	if (!args.set_version && !compiler->get_common_options().version) {
		fprintf(stderr, "Didn't specify GLSL version and SPIR-V did not specify language.\n");
		// print_help();
		// exit(EXIT_FAILURE);
	}

	spirv_cross::CompilerGLSL::Options opts = compiler->get_common_options();
	if (args.set_version) {
		opts.version = args.version;
	}
	if (args.set_es) {
		opts.es = args.es;
	}
	opts.force_temporary = args.force_temporary;
	opts.separate_shader_objects = args.sso;
	opts.flatten_multidimensional_arrays = args.flatten_multidimensional_arrays;
	opts.enable_420pack_extension = args.use_420pack_extension;
	opts.vulkan_semantics = args.vulkan_semantics;
	opts.vertex.fixup_clipspace = args.fixup;
	opts.vertex.flip_vert_y = args.yflip;
	opts.vertex.support_nonzero_base_instance = args.support_nonzero_baseinstance;
	opts.emit_push_constant_as_uniform_buffer = args.glsl_emit_push_constant_as_ubo;
	opts.emit_uniform_buffer_as_plain_uniforms = args.glsl_emit_ubo_as_plain_uniforms;
	opts.force_flattened_io_blocks = args.glsl_force_flattened_io_blocks;
	opts.ovr_multiview_view_count = args.glsl_ovr_multiview_view_count;
	opts.emit_line_directives = args.emit_line_directives;
	opts.enable_storage_image_qualifier_deduction = args.enable_storage_image_qualifier_deduction;
	opts.force_zero_initialized_variables = args.force_zero_initialized_variables;
	opts.relax_nan_checks = args.relax_nan_checks;
	opts.force_recompile_max_debug_iterations = args.force_recompile_max_debug_iterations;
	compiler->set_common_options(opts);

	// This is enough for Vulkan mapping API.
	// if (args.glsl_descriptor_heap_set != UINT32_MAX)
	// 	compiler->remap_descriptor_heap(ResourceTypeUnknown, args.glsl_descriptor_heap_set, args.glsl_descriptor_heap_binding);

	for (auto &fetch : args.glsl_ext_framebuffer_fetch) {
		compiler->remap_ext_framebuffer_fetch(fetch.first, fetch.second, !args.glsl_ext_framebuffer_fetch_noncoherent);
	}

	if (build_dummy_sampler) {
		uint32_t sampler = compiler->build_dummy_sampler_for_combined_images();
		if (sampler != 0) {
			// Set some defaults to make validation happy.
			compiler->set_decoration(sampler, spirv_cross::DecorationDescriptorSet, 0);
			compiler->set_decoration(sampler, spirv_cross::DecorationBinding, 0);
		}
	}

	spirv_cross::ShaderResources res;
	if (args.remove_unused) {
		auto active = compiler->get_active_interface_variables();
		res = compiler->get_shader_resources(active);
		compiler->set_enabled_interface_variables(std::move(active));
	} else {
		res = compiler->get_shader_resources();
	}

	if (args.flatten_ubo) {
		for (auto &ubo : res.uniform_buffers) {
			compiler->flatten_buffer_block(ubo.id);
		}
		for (auto &ubo : res.push_constant_buffers) {
			compiler->flatten_buffer_block(ubo.id);
		}
	}
	for (auto &ext : args.extensions) {
		compiler->require_extension(ext);
	}

	if (combined_image_samplers) {
		compiler->build_combined_image_samplers();
		// if (args.combined_samplers_inherit_bindings)
		// 	spirv_cross_util::inherit_combined_sampler_bindings(*compiler);

		// Give the remapped combined samplers new names.
		for (auto &remap : compiler->get_combined_image_samplers()) {
			compiler->set_name(remap.combined_id, spirv_cross::join("SPIRV_Cross_Combined", compiler->get_name(remap.image_id), compiler->get_name(remap.sampler_id)));
		}
	}

	if (entry_points.size() > 1) {
		throw std::runtime_error("Multiple entry points found in this shader, only one is expected!");
	}
	execution_model_str = execution_model_to_str(entry_points[0].execution_model);
	std::string ret = compiler->compile();

	return String::utf8(ret.c_str(), ret.size());
}
