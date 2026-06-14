#include "visual_shader_compat.h"
#include "core/error/error_macros.h"
#include "core/version_generated.gen.h"

void VisualShaderCompat::_update_shader() const {
	// does nothing; we don't want to update the shader code
}

void VisualShaderCompat::reset_state() {
	// does nothing; we don't want to reset the shader state
}

Shader::Mode VisualShaderCompat::get_mode() const {
	return Shader::get_mode();
}

bool VisualShaderCompat::is_text_shader() const {
	return true;
}

Ref<Resource> VisualShaderConverterCompat::convert(const Ref<MissingResource> &res, ResourceInfo::LoadType p_type, int ver_major, Error *r_error) {
	Ref<VisualShaderCompat> visual_shader = memnew(VisualShaderCompat());
	bool got = false;
	static const StringName res_format_type = "text";
	visual_shader->_start_load(res_format_type, 2);
	visual_shader->set_code(res->get("code", &got));
	if (!got || visual_shader->get_code().is_empty()) {
		if (r_error) {
			*r_error = ERR_FILE_CORRUPT;
		}
		ERR_FAIL_V_MSG(nullptr, "Failed to get code from VisualShader");
	}
	visual_shader->_finish_load(res_format_type, 2);
	visual_shader->merge_meta_from(res.ptr());
	return visual_shader;
}

bool VisualShaderConverterCompat::handles_type(const String &p_type, int ver_major) const {
	return p_type == "VisualShader" && ver_major < GODOT_VERSION_MAJOR;
}
