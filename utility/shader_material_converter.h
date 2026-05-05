#pragma once
#include "core/templates/pair.h"
#include "core/object/ref_counted.h"
#include "scene/resources/material.h"
class BaseMaterial3D;
class ShaderMaterial;
class Node;

namespace ShaderMaterialConverter {
	Pair<Ref<BaseMaterial3D>, Pair<bool, bool>> convert_shader_material_to_base_material(Ref<ShaderMaterial> p_shader_material, Node *p_parent = nullptr);
}
