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
		using Scope = ShaderLanguage::ShaderNode::Uniform::Scope;
		using DataType = ShaderLanguage::DataType;

		static const HashMap<String, Hint> hint_name_to_hint;
		static const HashMap<String, DataType> data_type_name_to_data_type;
		String name;
		Scope scope = Scope::SCOPE_LOCAL;
		DataType type = DataType::TYPE_VOID;
		Vector<Hint> hints;
		int array_size = 0;
		PropertyInfo property_info;

		Variant value;

		bool is_instance() {
			return scope == Scope::SCOPE_INSTANCE;
		}
		bool is_global() {
			return scope == Scope::SCOPE_GLOBAL;
		}
		bool is_local() {
			return scope == Scope::SCOPE_LOCAL;
		}
		bool is_texture() {
			return type == DataType::TYPE_SAMPLER2D;
		}

		bool is_array() {
			return array_size > 0;
		}

		static Scope parse_scope(const String &p_scope) {
			if (p_scope == "global") {
				return Scope::SCOPE_GLOBAL;
			} else if (p_scope == "instance") {
				return Scope::SCOPE_INSTANCE;
			} else {
				return Scope::SCOPE_LOCAL;
			}
		}

		static DataType parse_data_type(const String &p_type);

		static Vector<Hint> parse_hints(const String &p_hint);
	};

private:
	static HashMap<String, UniformInfo> parse_shader_uniforms(const Ref<ShaderMaterial> &p_shader_material);

public:
	using Hint = ShaderLanguage::ShaderNode::Uniform::Hint;
	static Pair<Ref<BaseMaterial3D>, Pair<bool, bool>> convert_shader_material_to_base_material(Ref<ShaderMaterial> p_shader_material, Node *p_parent = nullptr);
};
