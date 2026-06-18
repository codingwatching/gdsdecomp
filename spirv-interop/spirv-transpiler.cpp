#include "spirv-transpiler.h"
#include "thirdparty/spirv-cross/spirv_cross.hpp"
#include "thirdparty/spirv-cross/spirv_glsl.hpp"
#include "thirdparty/spirv-cross/spirv_parser.hpp"

namespace {
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
} //namespace

std::optional<std::string> spirv_interop::compile_iteration(const SPIRVCrossOptions &args, std::vector<uint32_t> spirv_file, std::string &execution_model_str, std::string &error_message) {
	try {
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
		return ret;
	} catch (const std::exception &e) {
		error_message = e.what();
		return std::nullopt;
	}
}
