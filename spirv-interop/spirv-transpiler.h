
#include <optional>
#include <string>
#include <vector>

namespace spirv_interop{

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

std::optional<std::string> compile_iteration(const SPIRVCrossOptions &args, std::vector<uint32_t> spirv_file, std::string &execution_model_str, std::string &error_message);

}
