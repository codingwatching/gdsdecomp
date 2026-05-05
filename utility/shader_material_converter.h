#pragma once
#include "core/object/ref_counted.h"
#include "core/templates/pair.h"
#include "scene/resources/material.h"
#include "servers/rendering/shader_language.h"
class BaseMaterial3D;
class ShaderMaterial;
class Node;

class ShaderMaterialConverter {
public:
	struct UniformInfo {
		using Hint = ShaderLanguage::ShaderNode::Uniform::Hint;

		static const HashMap<String, Hint> hint_name_to_hint;
		String scope;
		String type;
		String name;
		Vector<Hint> hints;
		bool is_instance() {
			return scope == "instance";
		}
		bool is_global() {
			return scope == "global";
		}
		bool is_local() {
			return scope == "local";
		}
		bool is_texture() {
			return type == "sampler2D";
		}

		static Vector<Hint> get_hints(const String &p_hint);
	};
	using Hint = ShaderLanguage::ShaderNode::Uniform::Hint;
	static Pair<Ref<BaseMaterial3D>, Pair<bool, bool>> convert_shader_material_to_base_material(Ref<ShaderMaterial> p_shader_material, Node *p_parent = nullptr);
};
