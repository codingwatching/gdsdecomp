#include "shader_material_converter.h"
#include "modules/regex/regex.h"
#include "core/config/project_settings.h"
#include "scene/resources/material.h"
#include "scene/resources/shader.h"
#include "scene/main/node.h"
#include "scene/resources/texture.h"

static const HashSet<String> shader_param_names = {
	"albedo",
	"specular",
	"metallic",
	"roughness",
	"emission",
	"emission_energy",
	"normal_scale",
	"rim",
	"rim_tint",
	"clearcoat",
	"clearcoat_roughness",
	"anisotropy",
	"heightmap_scale",
	"subsurface_scattering_strength",
	"transmittance_color",
	"transmittance_depth",
	"transmittance_boost",
	"backlight",
	"refraction",
	"point_size",
	"uv1_scale",
	"uv1_offset",
	"uv2_scale",
	"uv2_offset",
	"particles_anim_h_frames",
	"particles_anim_v_frames",
	"particles_anim_loop",
	"heightmap_min_layers",
	"heightmap_max_layers",
	"heightmap_flip",
	"uv1_blend_sharpness",
	"uv2_blend_sharpness",
	"grow",
	"proximity_fade_distance",
	"msdf_pixel_range",
	"msdf_outline_size",
	"distance_fade_min",
	"distance_fade_max",
	"ao_light_affect",
	"metallic_texture_channel",
	"ao_texture_channel",
	"clearcoat_texture_channel",
	"rim_texture_channel",
	"heightmap_texture_channel",
	"refraction_texture_channel",
	"alpha_scissor_threshold",
	"alpha_hash_scale",
	"alpha_antialiasing_edge",
	"albedo_texture_size",
	"z_clip_scale",
	"fov_override",
};

bool set_param_not_in_property_list(Ref<BaseMaterial3D> p_base_material, const String &p_param_name, const Variant &p_value) {
	// if the param is not in the property list, but it does have a setter, we can set it
	String setter_name = vformat("set_%s", p_param_name);
	bool is_valid = false;
	int arg_count = p_base_material->get_method_argument_count(setter_name, &is_valid);
	if (is_valid && arg_count == 1) {
		String getter_name = vformat("get_%s", p_param_name);
		if (p_base_material->get_method_argument_count(getter_name, &is_valid) == 0 && is_valid) {
			// check the method signature
			Variant base_material_val = p_base_material->call(getter_name);
			if (base_material_val.get_type() != p_value.get_type()) {
				return false;
			}
		}
		p_base_material->call(vformat("set_%s", p_param_name), p_value);
		return true;
	} else if (p_param_name == "metallness" && p_value.get_type() == Variant::FLOAT) {
		p_base_material->set_metallic(p_value);
		return true;
	}
	return false;
}
struct UniformInfo {
	String scope;
	String type;
	String name;
	String hint;
};

Pair<Ref<BaseMaterial3D>, Pair<bool, bool>> ShaderMaterialConverter::convert_shader_material_to_base_material(Ref<ShaderMaterial> p_shader_material, Node *p_parent) {
	// we need to manually create a BaseMaterial3D from the shader material
	// We do this by getting the shader uniforms and then mapping them to the BaseMaterial3D properties
	// We also need to handle the texture parameters and features
	ERR_FAIL_COND_V_MSG(p_shader_material.is_null(), {}, "ShaderMaterial is NULL");
	List<StringName> list;
	Ref<Shader> shader = p_shader_material->get_shader();
	ERR_FAIL_COND_V_MSG(shader.is_null(), {}, "Shader on ShaderMaterial is NULL: " + p_shader_material->get_path());
	List<PropertyInfo> prop_list;
	List<PropertyInfo> uniform_list;
	HashSet<String> shader_uniforms;
	shader->get_shader_uniform_list(&uniform_list, false);
	for (auto &E : uniform_list) {
		shader_uniforms.insert(E.name);
	}
	HashSet<String> global_uniforms;
	HashSet<String> instance_uniforms;
	//(global|instance)?\s*uniform\s+(\w+)\s+(\w+)\s*(?:\:\s+(\w+))?

	String shader_code = shader->get_code();
	Ref<RegEx> re = RegEx::create_from_string(R"((global|instance)?\s*uniform\s+(?:(?:lowp|mediump|highp)\s+)?(\w+)\s+(\w+)\s*(?:\:((?:\s*\w+\s*,)*\s*\w+))?)");
	// Don't match "normal" or "form"
	Ref<RegEx> orm_re = RegEx::create_from_string("(^ORM|[^NF]ORM[a-z_]|[a-z_]ORM|^orm|_orm|[^NnFf]orm_|Orm).*");
	auto is_orm = [&](const String &p_param_name) {
		return p_param_name == "orm" || orm_re->search(p_param_name).is_valid();
	};
	auto matches = re->search_all(shader_code);
	bool has_orm = false;
	HashMap<String, UniformInfo> uniform_infos;
	for (auto &E : matches) {
		Ref<RegExMatch> match = E;
		UniformInfo info;
		String param_name = match->get_string(3);

		info.scope = match->get_string(1);
		info.type = match->get_string(2);
		info.name = match->get_string(3);
		info.hint = match->get_string(4).strip_edges();
		uniform_infos[info.name] = info;
		shader_uniforms.insert(param_name);
		if (info.scope == "global") {
			global_uniforms.insert(param_name);
		} else if (info.scope == "instance") {
			instance_uniforms.insert(param_name);
		}
		if (info.type == "sampler2D") {
			if (is_orm(param_name)) {
				has_orm = true;
			}
		}
	}

	p_shader_material->get_property_list(&prop_list, false);
	for (auto &E : prop_list) {
		if (E.usage == PROPERTY_USAGE_CATEGORY || E.usage == PROPERTY_USAGE_GROUP || E.usage == PROPERTY_USAGE_SUBGROUP) {
			continue;
		}
		if (E.name.begins_with("shader_parameter/")) {
			shader_uniforms.insert(E.name.trim_prefix("shader_parameter/"));
		}
	}

	Ref<BaseMaterial3D> base_material = memnew(BaseMaterial3D(has_orm));

	// this is initialized with the 3.x params that are still valid in 4.x (`_set()` handles the remapping) but won't show up in the property list
	HashSet<String> base_material_params = {
		"flags_use_shadow_to_opacity",
		"flags_use_shadow_to_opacity",
		"flags_no_depth_test",
		"flags_use_point_size",
		"flags_fixed_size",
		"flags_albedo_tex_force_srgb",
		"flags_do_not_receive_shadows",
		"flags_disable_ambient_light",
		"params_diffuse_mode",
		"params_specular_mode",
		"params_blend_mode",
		"params_cull_mode",
		"params_depth_draw_mode",
		"params_point_size",
		"params_billboard_mode",
		"params_billboard_keep_scale",
		"params_grow",
		"params_grow_amount",
		"params_alpha_scissor_threshold",
		"params_alpha_hash_scale",
		"params_alpha_antialiasing_edge",
		"depth_scale",
		"depth_deep_parallax",
		"depth_min_layers",
		"depth_max_layers",
		"depth_flip_tangent",
		"depth_flip_binormal",
		"depth_texture",
		"emission_energy",
		"flags_transparent",
		"flags_unshaded",
		"flags_vertex_lighting",
		"params_use_alpha_scissor",
		"params_use_alpha_hash",
		"depth_enabled",
	};
	List<PropertyInfo> base_material_prop_list;
	base_material->get_property_list(&base_material_prop_list);
	bool hit_base_material_group = false;
	for (auto &E : base_material_prop_list) {
		if (E.usage == PROPERTY_USAGE_CATEGORY && E.name == "BaseMaterial3D") {
			hit_base_material_group = true;
		}
		if (!hit_base_material_group) {
			continue;
		}
		if (E.usage == PROPERTY_USAGE_CATEGORY || E.usage == PROPERTY_USAGE_GROUP || E.usage == PROPERTY_USAGE_SUBGROUP) {
			if (E.name != "StandardMaterial3D") {
				continue;
			}
			break;
		}
		base_material_params.insert(E.name);
	}

	// base shader switches the texture names in the shader
	static const HashMap<String, String> shader_texture_name_to_base_material_texture_param = {
		{ "texture_albedo", "albedo_texture" },
		{ "texture_orm", "orm_texture" },
		{ "texture_metallic", "metallic_texture" },
		{ "texture_roughness", "roughness_texture" },
		{ "texture_emission", "emission_texture" },
		{ "texture_normal", "normal_texture" },
		{ "texture_bent_normal", "bent_normal_texture" },
		{ "texture_rim", "rim_texture" },
		{ "texture_clearcoat", "clearcoat_texture" },
		{ "texture_flowmap", "anisotropy_flowmap" },
		{ "texture_ambient_occlusion", "ao_texture" },
		{ "texture_heightmap", "heightmap_texture" },
		{ "texture_subsurface_scattering", "subsurf_scatter_texture" },
		{ "texture_subsurface_transmittance", "subsurf_scatter_transmittance_texture" },
		{ "texture_backlight", "backlight_texture" },
		{ "texture_refraction", "refraction_texture" },
		{ "texture_detail_mask", "detail_mask" },
		{ "texture_detail_albedo", "detail_albedo" },
		{ "texture_detail_normal", "detail_normal" },
		{ "texture_depth", "depth_texture" },
		{ "texture_orm", "orm_texture" },
	};

	static const HashMap<String, BaseMaterial3D::TextureParam> texture_param_map = {
		{ "albedo_texture", BaseMaterial3D::TEXTURE_ALBEDO },
		{ "orm_texture", BaseMaterial3D::TEXTURE_ORM },
		{ "metallic_texture", BaseMaterial3D::TEXTURE_METALLIC },
		{ "roughness_texture", BaseMaterial3D::TEXTURE_ROUGHNESS },
		{ "emission_texture", BaseMaterial3D::TEXTURE_EMISSION },
		{ "normal_texture", BaseMaterial3D::TEXTURE_NORMAL },
		{ "bent_normal_texture", BaseMaterial3D::TEXTURE_BENT_NORMAL },
		{ "rim_texture", BaseMaterial3D::TEXTURE_RIM },
		{ "clearcoat_texture", BaseMaterial3D::TEXTURE_CLEARCOAT },
		{ "anisotropy_flowmap", BaseMaterial3D::TEXTURE_FLOWMAP },
		{ "ao_texture", BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION },
		{ "heightmap_texture", BaseMaterial3D::TEXTURE_HEIGHTMAP },
		{ "subsurf_scatter_texture", BaseMaterial3D::TEXTURE_SUBSURFACE_SCATTERING },
		{ "subsurf_scatter_transmittance_texture", BaseMaterial3D::TEXTURE_SUBSURFACE_TRANSMITTANCE },
		{ "backlight_texture", BaseMaterial3D::TEXTURE_BACKLIGHT },
		{ "refraction_texture", BaseMaterial3D::TEXTURE_REFRACTION },
		{ "detail_mask", BaseMaterial3D::TEXTURE_DETAIL_MASK },
		{ "detail_albedo", BaseMaterial3D::TEXTURE_DETAIL_ALBEDO },
		{ "detail_normal", BaseMaterial3D::TEXTURE_DETAIL_NORMAL },
		{ "depth_texture", BaseMaterial3D::TEXTURE_HEIGHTMAP }, // old 3.x name for heightmap texture
		{ "orm_texture", BaseMaterial3D::TEXTURE_ORM },
	};

	static const HashMap<BaseMaterial3D::TextureParam, BaseMaterial3D::Feature> feature_to_enable_map = {
		{ BaseMaterial3D::TEXTURE_NORMAL, BaseMaterial3D::FEATURE_NORMAL_MAPPING },
		{ BaseMaterial3D::TEXTURE_BENT_NORMAL, BaseMaterial3D::FEATURE_BENT_NORMAL_MAPPING },
		{ BaseMaterial3D::TEXTURE_EMISSION, BaseMaterial3D::FEATURE_EMISSION },
		{ BaseMaterial3D::TEXTURE_RIM, BaseMaterial3D::FEATURE_RIM },
		{ BaseMaterial3D::TEXTURE_CLEARCOAT, BaseMaterial3D::FEATURE_CLEARCOAT },
		{ BaseMaterial3D::TEXTURE_FLOWMAP, BaseMaterial3D::FEATURE_ANISOTROPY },
		{ BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION, BaseMaterial3D::FEATURE_AMBIENT_OCCLUSION },
		{ BaseMaterial3D::TEXTURE_HEIGHTMAP, BaseMaterial3D::FEATURE_HEIGHT_MAPPING },
		{ BaseMaterial3D::TEXTURE_SUBSURFACE_SCATTERING, BaseMaterial3D::FEATURE_SUBSURFACE_SCATTERING },
		{ BaseMaterial3D::TEXTURE_SUBSURFACE_TRANSMITTANCE, BaseMaterial3D::FEATURE_SUBSURFACE_TRANSMITTANCE },
		{ BaseMaterial3D::TEXTURE_BACKLIGHT, BaseMaterial3D::FEATURE_BACKLIGHT },
		{ BaseMaterial3D::TEXTURE_REFRACTION, BaseMaterial3D::FEATURE_REFRACTION },
		{ BaseMaterial3D::TEXTURE_DETAIL_MASK, BaseMaterial3D::FEATURE_DETAIL },
		{ BaseMaterial3D::TEXTURE_DETAIL_ALBEDO, BaseMaterial3D::FEATURE_DETAIL },
		{ BaseMaterial3D::TEXTURE_DETAIL_NORMAL, BaseMaterial3D::FEATURE_DETAIL },
	};

	Vector<String> lines = shader_code.split("\n");
	bool set_texture = false;
	HashMap<BaseMaterial3D::TextureParam, Vector<Pair<String, Ref<Texture>>>> base_material_texture_candidates;
	for (int i = 0; i < BaseMaterial3D::TEXTURE_MAX; i++) {
		base_material_texture_candidates[BaseMaterial3D::TextureParam(i)] = Vector<Pair<String, Ref<Texture>>>();
	}
	HashMap<String, Variant> set_params;
	HashMap<String, Variant> non_set_params;
	auto add_orm_texture_candidate = [&](const String &param_name, const Ref<Texture> &tex) {
		String lparam_name = param_name.to_lower();
		bool did_set = true;
		if (lparam_name.contains("metallic") || lparam_name.contains("metalness")) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_METALLIC].push_back(Pair<String, Ref<Texture>>(param_name, tex));
		} else if (lparam_name.contains("roughness") || lparam_name.contains("rough")) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_ROUGHNESS].push_back(Pair<String, Ref<Texture>>(param_name, tex));
		} else if (lparam_name.contains("ao") || lparam_name.contains("occlusion") || lparam_name.contains("ambient_occlusion")) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION].push_back(Pair<String, Ref<Texture>>(param_name, tex));
		} else if (is_orm(param_name)) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_ORM].push_back(Pair<String, Ref<Texture>>(param_name, tex));
		} else {
			did_set = false;
		}
		return did_set;
	};
	auto add_texture_candidate = [&](const String &param_name, const String &hint, const Ref<Texture> &tex) {
		String lparam_name = param_name.to_lower();
		if (lparam_name.contains("albedo")) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_ALBEDO].push_back(Pair<String, Ref<Texture>>(param_name, tex));
		} else if (lparam_name.contains("normal") && !hint.contains("roughness_normal")) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_NORMAL].push_back(Pair<String, Ref<Texture>>(param_name, tex));
		} else if (lparam_name.contains("emissive") || lparam_name.contains("emission")) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_EMISSION].push_back(Pair<String, Ref<Texture>>(param_name, tex));
		} else {
			return add_orm_texture_candidate(param_name, tex);
		}
		return true;
	};
	Vector<Pair<Pair<String, String>, Ref<Texture>>> unknown_texture_candidates;
	for (auto &E : shader_uniforms) {
		String param_name = E.trim_prefix("shader_parameter/");
		Variant val = p_shader_material->get_shader_parameter(param_name);

		bool did_set = false;
		if (base_material_params.has(param_name)) {
			Variant base_material_val;
			if (global_uniforms.has(param_name)) {
				base_material_val = ProjectSettings::get_singleton()->get_setting("shader_globals/" + param_name);
			} else if (instance_uniforms.has(param_name) && p_parent) {
				bool valid = false;
				Variant parent_val = p_parent->get(param_name, &valid);
				if (valid) {
					base_material_val = parent_val;
				}
			} else {
				base_material_val = base_material->get(param_name);
			}

			if (base_material_val.get_type() == val.get_type()) {
				base_material->set(param_name, val);
				did_set = true;
				set_params[param_name] = val;
			}
		} else if (set_param_not_in_property_list(base_material, param_name, val)) {
			did_set = true;
			set_params[param_name] = val;
		}
		if (!did_set) {
			non_set_params[param_name] = val;
		}

		Ref<Texture> tex = val;
		if (!tex.is_valid()) {
			continue;
		}
		if (did_set) {
			set_texture = true;
			auto param = texture_param_map.has(param_name) ? texture_param_map.get(param_name) : BaseMaterial3D::TEXTURE_MAX;
			if (param == BaseMaterial3D::TEXTURE_MAX && shader_texture_name_to_base_material_texture_param.has(param_name)) {
				param = texture_param_map.get(shader_texture_name_to_base_material_texture_param.get(param_name));
			}

			if (param != BaseMaterial3D::TEXTURE_MAX) {
				base_material_texture_candidates[param].push_back(Pair<String, Ref<Texture>>(param_name, tex));
			} else {
				WARN_PRINT(vformat("Unknown texture parameter: %s", param_name));
			}
			continue;
		}

		String hint;
		// trying to capture "uniform sampler2D emissive_texture : hint_black;"
		if (uniform_infos.has(param_name)) {
			hint = uniform_infos[param_name].hint;
		}

		// renames from 3.x to 4.x
		// { "hint_albedo", "source_color" },
		// { "hint_aniso", "hint_anisotropy" },
		// { "hint_black", "hint_default_black" },
		// { "hint_black_albedo", "hint_default_black" },
		// { "hint_color", "source_color" },
		// { "hint_white", "hint_default_white" },
		auto lparam_name = param_name.to_lower();

		if (hint.contains("hint_albedo") || hint.contains("source_color")) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_ALBEDO].push_back(Pair<String, Ref<Texture>>(param_name, tex));
		} else if (hint.contains("hint_white") || hint.contains("hint_default_white")) {
			add_orm_texture_candidate(param_name, tex);
		} else if (hint.contains("hint_normal")) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_NORMAL].push_back(Pair<String, Ref<Texture>>(param_name, tex));
		} else if (hint.contains("hint_black") || hint.contains("hint_default_black")) {
			// if (param_name.contains("emissive") || param_name.contains("emission")) {
			// 	base_material_texture_candidates[BaseMaterial3D::TEXTURE_EMISSION].push_back(Pair<String, Ref<Texture>>(param_name, tex));
			// } else {
			// 	default_textures[param_name.trim_prefix("shader_parameter/")] = tex;
			// }
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_EMISSION].push_back(Pair<String, Ref<Texture>>(param_name, tex));
		} else {
			unknown_texture_candidates.push_back({ { param_name, hint }, tex });
		}
	}
	for (auto &E : unknown_texture_candidates) {
		add_texture_candidate(E.first.first, E.first.second, E.second);
	}
	for (int i = 0; i < BaseMaterial3D::TEXTURE_MAX; i++) {
		auto &candidates = base_material_texture_candidates[BaseMaterial3D::TextureParam(i)];
		auto existing_tex = base_material->get_texture(BaseMaterial3D::TextureParam(i));

		bool should_not_set = candidates.is_empty() && !existing_tex.is_valid();
		if (should_not_set) {
			continue;
		}

		if (feature_to_enable_map.has(BaseMaterial3D::TextureParam(i))) {
			base_material->set_feature(feature_to_enable_map[BaseMaterial3D::TextureParam(i)], true);
		}

		if (existing_tex.is_valid()) {
			continue;
		}
		if (candidates.size() == 1) {
			base_material->set_texture(BaseMaterial3D::TextureParam(i), candidates[0].second);
			set_texture = true;
			set_params[candidates[0].first] = candidates[0].second;
			continue;
		}
		bool set_this_texture = false;
		if (i == BaseMaterial3D::TEXTURE_ALBEDO) {
			for (auto &E : candidates) {
				if (E.first.contains("albedo")) {
					base_material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, E.second);
					set_texture = true;
					set_this_texture = true;
					set_params[E.first] = E.second;
				} else if (E.first.contains("emissi") && set_this_texture) {
					// push this back to the emission candidates
					base_material_texture_candidates[BaseMaterial3D::TEXTURE_EMISSION].push_back(E);
				}
			}
		} else if (i == BaseMaterial3D::TEXTURE_NORMAL) {
			for (auto &E : candidates) {
				if (E.first.contains("normal")) {
					base_material->set_texture(BaseMaterial3D::TEXTURE_NORMAL, E.second);
					set_texture = true;
					set_this_texture = true;
					set_params[E.first] = E.second;
				}
			}
		}
		if (!set_this_texture) {
			// just use the first one
			base_material->set_texture(BaseMaterial3D::TextureParam(i), candidates[0].second);
			set_texture = true;
			set_params[candidates[0].first] = candidates[0].second;
		}
	}
	if (shader_uniforms.has("brightness") && p_shader_material->get_shader_parameter("brightness").get_type() == Variant::FLOAT) {
		float brightness = p_shader_material->get_shader_parameter("brightness");
		Color get_emission = base_material->get_emission();
		if (get_emission == Color()) {
			get_emission = base_material->get_albedo();
		}
		base_material->set_emission(get_emission * brightness);
	}
	if (shader_uniforms.has("alpha")) {
		Variant alpha_val = p_shader_material->get_shader_parameter("alpha");
		if (alpha_val.get_type() == Variant::FLOAT) {
			base_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
			base_material->set_alpha_antialiasing(BaseMaterial3D::ALPHA_ANTIALIASING_ALPHA_TO_COVERAGE);
			Color albedo = base_material->get_albedo();
			albedo.a = alpha_val;
			base_material->set_albedo(albedo);
		}
	}
	base_material->set_name(p_shader_material->get_name());
	// set the path to the shader material's path so that we can add it to the external deps found if necessary
	base_material->set_path_cache(p_shader_material->get_path());
	base_material->merge_meta_from(p_shader_material.ptr());

	bool set_instance_uniforms = false;
	for (auto &E : set_params) {
		if (instance_uniforms.has(E.key)) {
			set_instance_uniforms = true;
			break;
		}
	}
	return { base_material, { set_texture, set_instance_uniforms } };
}
