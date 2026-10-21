/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "moa_mod/objects/TemplateBuilder.h"

#include <format>
#include <utility>

#include <spdlog/spdlog.h>

namespace moa_mod::objects
{
namespace
{

/// `UnityEngine.Rendering.IndexFormat.UInt32`.
constexpr int32_t k_IndexFormat32 = 1;

/// `UnityEngine.TextureFormat.RGBA32`. `ImageConversion.LoadImage` sets the format of the image.
constexpr int32_t k_TextureFormatRgba32 = 4;

/// The size of a texture before `LoadImage` gives it the size of the image.
constexpr int32_t k_EmptyTextureSide = 2;

/// The name of the child that shows the mesh of the pack.
constexpr const char* k_VisualName = "Visual";

/// The bounds of a mesh as Unity keeps them: the centre and the extents.
struct UnityBounds
{
	std::array<float, 3> centre;
	std::array<float, 3> extents;
};

} // namespace

moa::Result<TemplateBindings> TemplateBindings::Bind(const Managed& managed)
{
	Binder binder(managed);
	TemplateBindings b;
	b.vector2 = binder.Class("UnityEngine", "Vector2");
	b.vector3 = binder.Class("UnityEngine", "Vector3");
	b.vector4 = binder.Class("UnityEngine", "Vector4");
	b.colour = binder.Class("UnityEngine", "Color");
	b.int32 = binder.Class("System", "Int32");
	b.byte = binder.Class("System", "Byte");
	b.gameObject = binder.Class("UnityEngine", "GameObject");
	b.mesh = binder.Class("UnityEngine", "Mesh");
	b.material = binder.Class("UnityEngine", "Material");
	b.texture2d = binder.Class("UnityEngine", "Texture2D");
	b.renderer = binder.Class("UnityEngine", "Renderer");
	b.meshFilter = binder.Class("UnityEngine", "MeshFilter");
	b.meshRenderer = binder.Class("UnityEngine", "MeshRenderer");
	b.lodGroup = binder.Class("UnityEngine", "LODGroup");
	b.boxCollider = binder.Class("UnityEngine", "BoxCollider");
	b.meshCollider = binder.Class("UnityEngine", "MeshCollider");
	auto* unityObject = binder.Class("UnityEngine", "Object");
	auto* component = binder.Class("UnityEngine", "Component");
	auto* transform = binder.Class("UnityEngine", "Transform");
	auto* collider = binder.Class("UnityEngine", "Collider");
	auto* imageConversion = binder.Class("UnityEngine", "ImageConversion");

	b.meshMake = binder.Method(b.mesh, ".ctor", {}, "System.Void");
	b.setIndexFormat = binder.Method(b.mesh, "set_indexFormat", {"UnityEngine.Rendering.IndexFormat"}, "System.Void");
	b.setVertices = binder.Method(b.mesh, "set_vertices", {"UnityEngine.Vector3[]"}, "System.Void");
	b.setNormals = binder.Method(b.mesh, "set_normals", {"UnityEngine.Vector3[]"}, "System.Void");
	b.setTangents = binder.Method(b.mesh, "set_tangents", {"UnityEngine.Vector4[]"}, "System.Void");
	b.setUv = binder.Method(b.mesh, "set_uv", {"UnityEngine.Vector2[]"}, "System.Void");
	b.setUv2 = binder.Method(b.mesh, "set_uv2", {"UnityEngine.Vector2[]"}, "System.Void");
	b.setColours = binder.Method(b.mesh, "set_colors", {"UnityEngine.Color[]"}, "System.Void");
	b.setSubMeshCount = binder.Method(b.mesh, "set_subMeshCount", {"System.Int32"}, "System.Void");
	b.setTriangles = binder.Method(b.mesh, "SetTriangles", {"System.Int32[]", "System.Int32"}, "System.Void");
	b.setMeshBounds = binder.Method(b.mesh, "set_bounds", {"UnityEngine.Bounds"}, "System.Void");
	b.uploadMeshData = binder.Method(b.mesh, "UploadMeshData", {"System.Boolean"}, "System.Void");

	b.materialCopy = binder.Method(b.material, ".ctor", {"UnityEngine.Material"}, "System.Void");
	b.setTexture = binder.Method(b.material, "SetTexture", {"System.String", "UnityEngine.Texture"}, "System.Void");
	b.hasProperty = binder.Method(b.material, "HasProperty", {"System.String"}, "System.Boolean");
	b.disableKeyword = binder.Method(b.material, "DisableKeyword", {"System.String"}, "System.Void");
	b.textureMake = binder.Method(
	    b.texture2d, ".ctor", {"System.Int32", "System.Int32", "UnityEngine.TextureFormat", "System.Boolean", "System.Boolean"},
	    "System.Void");
	b.loadImage =
	    binder.Method(imageConversion, "LoadImage", {"UnityEngine.Texture2D", "System.Byte[]"}, "System.Boolean", true);
	b.whiteTexture = binder.Method(b.texture2d, "get_whiteTexture", {}, "UnityEngine.Texture2D", true);
	b.blackTexture = binder.Method(b.texture2d, "get_blackTexture", {}, "UnityEngine.Texture2D", true);

	b.gameObjectMake = binder.Method(b.gameObject, ".ctor", {"System.String"}, "System.Void");
	b.getComponentsInChildren =
	    binder.Method(b.gameObject, "GetComponentsInChildren", {"System.Type", "System.Boolean"}, "UnityEngine.Component[]");
	b.getComponent = binder.Method(b.gameObject, "GetComponent", {"System.Type"}, "UnityEngine.Component");
	b.addComponent = binder.Method(b.gameObject, "AddComponent", {"System.Type"}, "UnityEngine.Component");
	b.gameObjectTransform = binder.Method(b.gameObject, "get_transform", {}, "UnityEngine.Transform");
	b.getLayer = binder.Method(b.gameObject, "get_layer", {}, "System.Int32");
	b.setLayer = binder.Method(b.gameObject, "set_layer", {"System.Int32"}, "System.Void");
	b.componentGameObject = binder.Method(component, "get_gameObject", {}, "UnityEngine.GameObject");
	b.setParent = binder.Method(transform, "SetParent", {"UnityEngine.Transform", "System.Boolean"}, "System.Void");
	b.setLocalPosition = binder.Method(transform, "set_localPosition", {"UnityEngine.Vector3"}, "System.Void");
	b.setLocalScale = binder.Method(transform, "set_localScale", {"UnityEngine.Vector3"}, "System.Void");
	b.destroyImmediate = binder.Method(unityObject, "DestroyImmediate", {"UnityEngine.Object"}, "System.Void", true);
	b.setName = binder.Method(unityObject, "set_name", {"System.String"}, "System.Void");

	b.getSharedMaterial = binder.Method(b.renderer, "get_sharedMaterial", {}, "UnityEngine.Material");
	b.setSharedMaterial = binder.Method(b.renderer, "set_sharedMaterial", {"UnityEngine.Material"}, "System.Void");
	b.setSharedMaterials = binder.Method(b.renderer, "set_sharedMaterials", {"UnityEngine.Material[]"}, "System.Void");
	b.setSharedMesh = binder.Method(b.meshFilter, "set_sharedMesh", {"UnityEngine.Mesh"}, "System.Void");
	b.setBoxCentre = binder.Method(b.boxCollider, "set_center", {"UnityEngine.Vector3"}, "System.Void");
	b.setBoxSize = binder.Method(b.boxCollider, "set_size", {"UnityEngine.Vector3"}, "System.Void");
	b.setColliderEnabled = binder.Method(collider, "set_enabled", {"System.Boolean"}, "System.Void");
	b.setColliderMesh = binder.Method(b.meshCollider, "set_sharedMesh", {"UnityEngine.Mesh"}, "System.Void");
	b.setConvex = binder.Method(b.meshCollider, "set_convex", {"System.Boolean"}, "System.Void");
	if (!binder.Errors().empty())
	{
		return moa::MakeError("{}", binder.Errors());
	}

	Binder kinds(managed);
	b.transform = transform;
	b.collider = collider;
	b.decoration = kinds.Class("", "NGDecoration");
	b.block = kinds.Class("", "Block");
	auto* legacyBase = kinds.Class("", "NGLegacyBase");
	b.getName = kinds.Method(unityObject, "get_name", {}, "System.String");
	b.transformPoint = kinds.Method(transform, "TransformPoint", {"UnityEngine.Vector3"}, "UnityEngine.Vector3");
	b.setPosition = kinds.Method(transform, "set_position", {"UnityEngine.Vector3"}, "System.Void");
	b.getIsTrigger = kinds.Method(collider, "get_isTrigger", {}, "System.Boolean");
	if (legacyBase != nullptr)
	{
		b.legacyName = kinds.Field(legacyBase, "m_name", "System.String");
	}
	if (b.block != nullptr)
	{
		b.blockInfoId = kinds.Field(b.block, "m_blockInfoID", "System.String");
	}
	b.kindErrors = kinds.Errors();
	if (auto navBlocker = managed.Class("", "BuildingNavBlocker"))
	{
		b.navBlocker = *navBlocker;
	}
	if (auto cylinder = managed.Class("", "CylinderCollider"))
	{
		b.cylinderCollider = *cylinder;
	}
	if (managed.il2cpp_array_new == nullptr || managed.il2cpp_array_object_header_size == nullptr ||
	    managed.il2cpp_class_array_element_size == nullptr || managed.il2cpp_array_length == nullptr ||
	    managed.il2cpp_object_new == nullptr)
	{
		return moa::MakeError("GameAssembly.dll does not export the array functions or il2cpp_object_new");
	}
	// The script is optional: an update of the game can remove it.
	if (auto holder = managed.Class("", "BatchInfoHolder"))
	{
		b.batchInfoHolder = *holder;
	}
	return b;
}

TemplateBuilder::TemplateBuilder(const Managed& managed, TemplateBindings bindings, KeepFunction keep)
    : _managed(managed)
    , _b(bindings)
    , _keep(std::move(keep))
{
}

moa::Result<Il2CppObject*> TemplateBuilder::Components(Il2CppObject* gameObject, Il2CppClass* klass)
{
	bool includeInactive = true;
	return _managed.Invoke(_b.getComponentsInChildren, gameObject, {_managed.TypeObject(klass), &includeInactive});
}

moa::Result<nlohmann::json> TemplateBuilder::Build(Il2CppObject* root, const PackResource& resource, const ResourceModel& model)
{
	nlohmann::json report;
	// The material and the layer of the first renderer of the template. The new visual uses them.
	auto renderers = Components(root, _b.renderer);
	if (!renderers)
	{
		return renderers.GetError();
	}
	const auto rendererCount = _managed.ArrayLength(*renderers);
	if (rendererCount == 0)
	{
		return moa::MakeError("the template has no renderer, so the mod has no material to copy");
	}
	auto* firstRenderer = _managed.ArrayItem(*renderers, 0);
	auto templateMaterial = _managed.Invoke(_b.getSharedMaterial, firstRenderer);
	if (!templateMaterial)
	{
		return templateMaterial.GetError();
	}
	if (*templateMaterial == nullptr)
	{
		return moa::MakeError("the first renderer of the template has no material");
	}
	auto rendererObject = _managed.Invoke(_b.componentGameObject, firstRenderer);
	if (!rendererObject)
	{
		return rendererObject.GetError();
	}
	auto layer = _managed.Call<int32_t>(_b.getLayer, *rendererObject);
	if (!layer)
	{
		return layer.GetError();
	}

	// Make the new parts first. If one of them fails, the template stays as it was.
	auto mesh = MakeMesh(model.mesh, resource.name);
	if (!mesh)
	{
		return mesh.GetError();
	}
	std::vector<Il2CppObject*> materials;
	nlohmann::json materialReport = nlohmann::json::array();
	for (size_t i = 0; i < model.mesh.subMeshTriangles.size(); ++i)
	{
		if (model.materials.empty())
		{
			materials.push_back(*templateMaterial);
			materialReport.push_back("template");
			continue;
		}
		const auto& images = model.materials[i];
		auto material =
		    MakeMaterial(*templateMaterial, images,
		                 std::format("{}_{}", resource.name, images.name.empty() ? std::to_string(i) : images.name));
		if (!material)
		{
			return material.GetError();
		}
		materials.push_back(*material);
		nlohmann::json textures = nlohmann::json::array();
		for (const auto& slot : TextureSlots(images))
		{
			textures.push_back(slot.property);
		}
		materialReport.push_back(textures);
	}

	// Remove the visual parts of the template: the renderers, the mesh filters, the scripts of the game on them and
	// the levels of detail. The empty game objects stay, so that the scripts of the template find their children.
	size_t removed = 0;
	for (auto* klass : {_b.lodGroup, _b.batchInfoHolder, _b.renderer, _b.meshFilter})
	{
		if (klass == nullptr)
		{
			continue;
		}
		auto parts = Components(root, klass);
		if (!parts)
		{
			return parts.GetError();
		}
		for (size_t i = 0; i < _managed.ArrayLength(*parts); ++i)
		{
			if (auto result = _managed.Invoke(_b.destroyImmediate, nullptr, {_managed.ArrayItem(*parts, i)}); !result)
			{
				return result.GetError();
			}
			++removed;
		}
	}

	// Add the child that shows the new mesh. A block keeps its visual parts under "Visuals", because the game looks for
	// that child when it makes the block.
	Il2CppObject* visualParent = nullptr;
	if (resource.kind == PackKind::Weapon)
	{
		auto found = FindTransform(root, k_VisualsName);
		if (!found)
		{
			return found.GetError();
		}
		if (*found == nullptr)
		{
			return moa::MakeError("the template has no child {}", k_VisualsName);
		}
		visualParent = *found;
	}
	auto* visual = _managed.il2cpp_object_new(_b.gameObject);
	if (visual == nullptr)
	{
		return moa::MakeError("the mod cannot make a game object");
	}
	if (auto made = _managed.Invoke(_b.gameObjectMake, visual, {_managed.String(k_VisualName)}); !made)
	{
		return made.GetError();
	}
	auto visualTransform = _managed.Invoke(_b.gameObjectTransform, visual);
	auto rootTransform = _managed.Invoke(_b.gameObjectTransform, root);
	if (!visualTransform || !rootTransform)
	{
		return moa::MakeError("the mod cannot get the transforms of the prefab");
	}
	bool keepWorldPosition = false;
	if (auto parented = _managed.Invoke(_b.setParent, *visualTransform,
	                                    {visualParent != nullptr ? visualParent : *rootTransform, &keepWorldPosition});
	    !parented)
	{
		return parented.GetError();
	}
	std::array<float, 3> origin {0.0f, 0.0f, 0.0f};
	std::array<float, 3> scale {resource.scale, resource.scale, resource.scale};
	if (auto moved = _managed.Invoke(_b.setLocalPosition, *visualTransform, {origin.data()}); !moved)
	{
		return moved.GetError();
	}
	if (auto scaled = _managed.Invoke(_b.setLocalScale, *visualTransform, {scale.data()}); !scaled)
	{
		return scaled.GetError();
	}
	if (auto layered = _managed.Invoke(_b.setLayer, visual, {&*layer}); !layered)
	{
		return layered.GetError();
	}
	auto filter = _managed.Invoke(_b.addComponent, visual, {_managed.TypeObject(_b.meshFilter)});
	if (!filter || *filter == nullptr)
	{
		return moa::MakeError("the mod cannot add a MeshFilter");
	}
	if (auto set = _managed.Invoke(_b.setSharedMesh, *filter, {*mesh}); !set)
	{
		return set.GetError();
	}
	auto renderer = _managed.Invoke(_b.addComponent, visual, {_managed.TypeObject(_b.meshRenderer)});
	if (!renderer || *renderer == nullptr)
	{
		return moa::MakeError("the mod cannot add a MeshRenderer");
	}
	if (materials.size() == 1)
	{
		if (auto set = _managed.Invoke(_b.setSharedMaterial, *renderer, {materials.front()}); !set)
		{
			return set.GetError();
		}
	}
	else
	{
		auto array = _managed.NewArray(_b.material, materials.size(), sizeof(Il2CppObject*));
		if (!array)
		{
			return array.GetError();
		}
		for (size_t i = 0; i < materials.size(); ++i)
		{
			_managed.SetArrayItem(*array, i, materials[i]);
		}
		if (auto set = _managed.Invoke(_b.setSharedMaterials, *renderer, {*array}); !set)
		{
			return set.GetError();
		}
	}

	if (resource.kind != PackKind::Resource && resource.collider != ColliderMode::Template)
	{
		auto disabled = DisableTemplateColliders(root);
		if (!disabled)
		{
			return disabled.GetError();
		}
		report["templateCollidersOff"] = *disabled;
	}
	if (auto fitted = FitCollider(root, visual, *mesh, resource, model.mesh, report); !fitted)
	{
		return fitted.GetError();
	}
	if (auto finished = FinishKind(root, resource, model, report); !finished)
	{
		return finished.GetError();
	}
	const auto size =
	    std::array {model.mesh.boundsExtents[0] * 2.0f * resource.scale, model.mesh.boundsExtents[1] * 2.0f * resource.scale,
	                model.mesh.boundsExtents[2] * 2.0f * resource.scale};
	report["vertices"] = model.mesh.vertices.size();
	report["subMeshes"] = model.mesh.subMeshTriangles.size();
	report["materials"] = materialReport;
	report["removed"] = removed;
	report["layer"] = *layer;
	report["size"] = size;
	if (resource.kind == PackKind::Resource)
	{
		report["grip"] = GripPoint(model.mesh, resource.scale);
	}
	return report;
}

moa::Result<Il2CppObject*> TemplateBuilder::MakeMesh(const moa::unitymesh::RuntimeMesh& source, const std::string& name)
{
	auto* mesh = _managed.il2cpp_object_new(_b.mesh);
	if (mesh == nullptr)
	{
		return moa::MakeError("the mod cannot make a mesh");
	}
	if (auto made = _managed.Invoke(_b.meshMake, mesh); !made)
	{
		return made.GetError();
	}
	(void)_managed.Invoke(_b.setName, mesh, {_managed.String(name.c_str())});
	_keep(mesh);
	// The index format must change before the triangles go in.
	if (source.index32)
	{
		auto format = k_IndexFormat32;
		if (auto set = _managed.Invoke(_b.setIndexFormat, mesh, {&format}); !set)
		{
			return set.GetError();
		}
	}
	// Each array goes in only when the runtime mesh has it. Unity checks that each array has one item for each
	// vertex, so the vertices go in first.
	const auto put = [&](const MethodInfo* setter, Il2CppClass* element, auto items) -> moa::Result<void> {
		if (items.empty())
		{
			return {};
		}
		auto array = _managed.ValueArray(element, std::span(items));
		if (!array)
		{
			return array.GetError();
		}
		if (auto set = _managed.Invoke(setter, mesh, {*array}); !set)
		{
			return set.GetError();
		}
		return {};
	};
	for (auto result :
	     {put(_b.setVertices, _b.vector3, std::span(source.vertices)),
	      put(_b.setNormals, _b.vector3, std::span(source.normals)),
	      put(_b.setTangents, _b.vector4, std::span(source.tangents)), put(_b.setUv, _b.vector2, std::span(source.uv0)),
	      put(_b.setUv2, _b.vector2, std::span(source.uv1)), put(_b.setColours, _b.colour, std::span(source.colours))})
	{
		if (!result)
		{
			return result.GetError();
		}
	}
	auto count = static_cast<int32_t>(source.subMeshTriangles.size());
	if (auto set = _managed.Invoke(_b.setSubMeshCount, mesh, {&count}); !set)
	{
		return set.GetError();
	}
	for (int32_t i = 0; i < count; ++i)
	{
		auto indices = ManagedIndices(source.subMeshTriangles[static_cast<size_t>(i)], source.vertices.size());
		if (!indices)
		{
			return moa::MakeError("sub-mesh {}: {}", i, indices.GetError().message);
		}
		auto array = _managed.ValueArray(_b.int32, std::span<const int32_t>(*indices));
		if (!array)
		{
			return array.GetError();
		}
		if (auto set = _managed.Invoke(_b.setTriangles, mesh, {*array, &i}); !set)
		{
			return set.GetError();
		}
	}
	UnityBounds bounds {.centre = source.boundsCentre, .extents = source.boundsExtents};
	if (auto set = _managed.Invoke(_b.setMeshBounds, mesh, {&bounds}); !set)
	{
		return set.GetError();
	}
	// The mesh stays readable, because a mesh collider reads it.
	bool noLongerReadable = false;
	if (auto uploaded = _managed.Invoke(_b.uploadMeshData, mesh, {&noLongerReadable}); !uploaded)
	{
		return uploaded.GetError();
	}
	return mesh;
}

moa::Result<Il2CppObject*> TemplateBuilder::MakeTexture(const TextureSlot& slot, const std::string& name)
{
	auto* texture = _managed.il2cpp_object_new(_b.texture2d);
	if (texture == nullptr)
	{
		return moa::MakeError("the mod cannot make a texture");
	}
	auto side = k_EmptyTextureSide;
	auto format = k_TextureFormatRgba32;
	bool mipmaps = true;
	bool linear = slot.linear;
	if (auto made = _managed.Invoke(_b.textureMake, texture, {&side, &side, &format, &mipmaps, &linear}); !made)
	{
		return made.GetError();
	}
	_keep(texture);
	auto bytes = _managed.ValueArray(_b.byte, slot.png);
	if (!bytes)
	{
		return bytes.GetError();
	}
	auto loaded = _managed.Call<bool>(_b.loadImage, nullptr, {texture, *bytes});
	if (!loaded)
	{
		return loaded.GetError();
	}
	if (!*loaded)
	{
		return moa::MakeError("Unity did not read the image {}", name);
	}
	(void)_managed.Invoke(_b.setName, texture, {_managed.String(name.c_str())});
	return texture;
}

moa::Result<Il2CppObject*> TemplateBuilder::MakeMaterial(Il2CppObject* source, const MaterialImages& images,
                                                         const std::string& name)
{
	auto* material = _managed.il2cpp_object_new(_b.material);
	if (material == nullptr)
	{
		return moa::MakeError("the mod cannot make a material");
	}
	if (auto made = _managed.Invoke(_b.materialCopy, material, {source}); !made)
	{
		return made.GetError();
	}
	(void)_managed.Invoke(_b.setName, material, {_managed.String(name.c_str())});
	_keep(material);
	const auto hasProperty = [&](std::string_view property) {
		auto has = _managed.Call<bool>(_b.hasProperty, material, {_managed.String(std::string(property).c_str())});
		return has && *has;
	};
	for (const auto& slot : TextureSlots(images))
	{
		if (!hasProperty(slot.property))
		{
			spdlog::warn("objects: the material {} has no property {}; the texture is not used", name, slot.property);
			continue;
		}
		auto texture = MakeTexture(slot, std::format("{}{}", name, slot.property));
		if (!texture)
		{
			return texture.GetError();
		}
		if (auto set =
		        _managed.Invoke(_b.setTexture, material, {_managed.String(std::string(slot.property).c_str()), *texture});
		    !set)
		{
			return set.GetError();
		}
	}
	// The tint mask and the emission map of the template belong to the art of the template.
	for (const auto& [property, getter] :
	     {std::pair {k_TintMapProperty, _b.whiteTexture}, std::pair {k_EmissionMapProperty, _b.blackTexture}})
	{
		if (!hasProperty(property))
		{
			continue;
		}
		auto texture = _managed.Invoke(getter, nullptr);
		if (!texture)
		{
			return texture.GetError();
		}
		if (auto set = _managed.Invoke(_b.setTexture, material, {_managed.String(std::string(property).c_str()), *texture});
		    !set)
		{
			return set.GetError();
		}
	}
	for (const auto keyword : k_KeywordsOff)
	{
		if (auto off = _managed.Invoke(_b.disableKeyword, material, {_managed.String(std::string(keyword).c_str())}); !off)
		{
			return off.GetError();
		}
	}
	return material;
}

moa::Result<Il2CppObject*> TemplateBuilder::FindTransform(Il2CppObject* root, std::string_view name)
{
	auto transforms = Components(root, _b.transform);
	if (!transforms)
	{
		return transforms.GetError();
	}
	for (size_t i = 0; i < _managed.ArrayLength(*transforms); ++i)
	{
		auto* transform = _managed.ArrayItem(*transforms, i);
		auto text = _managed.Invoke(_b.getName, transform);
		if (text && *text != nullptr && _managed.Text(*text) == name)
		{
			return transform;
		}
	}
	return static_cast<Il2CppObject*>(nullptr);
}

moa::Result<size_t> TemplateBuilder::DisableTemplateColliders(Il2CppObject* root)
{
	size_t count = 0;
	if (_b.cylinderCollider != nullptr)
	{
		auto scripts = Components(root, _b.cylinderCollider);
		if (!scripts)
		{
			return scripts.GetError();
		}
		for (size_t i = 0; i < _managed.ArrayLength(*scripts); ++i)
		{
			if (auto result = _managed.Invoke(_b.destroyImmediate, nullptr, {_managed.ArrayItem(*scripts, i)}); !result)
			{
				return result.GetError();
			}
			++count;
		}
	}
	auto colliders = Components(root, _b.collider);
	if (!colliders)
	{
		return colliders.GetError();
	}
	for (size_t i = 0; i < _managed.ArrayLength(*colliders); ++i)
	{
		auto* collider = _managed.ArrayItem(*colliders, i);
		auto trigger = _managed.Call<bool>(_b.getIsTrigger, collider);
		if (!trigger)
		{
			return trigger.GetError();
		}
		if (*trigger)
		{
			continue;
		}
		if (_b.navBlocker != nullptr)
		{
			auto owner = _managed.Invoke(_b.componentGameObject, collider);
			auto blocker = owner ? _managed.Invoke(_b.getComponent, *owner, {_managed.TypeObject(_b.navBlocker)})
			                     : moa::Result<Il2CppObject*>(owner.GetError());
			if (blocker && *blocker != nullptr)
			{
				continue;
			}
		}
		bool enabled = false;
		if (auto set = _managed.Invoke(_b.setColliderEnabled, collider, {&enabled}); !set)
		{
			return set.GetError();
		}
		++count;
	}
	return count;
}

moa::Result<size_t> TemplateBuilder::MovePoints(Il2CppObject* root, std::string_view name, const std::array<float, 3>& point)
{
	auto rootTransform = _managed.Invoke(_b.gameObjectTransform, root);
	if (!rootTransform)
	{
		return rootTransform.GetError();
	}
	auto copy = point;
	auto world = _managed.Call<std::array<float, 3>>(_b.transformPoint, *rootTransform, {copy.data()});
	if (!world)
	{
		return world.GetError();
	}
	auto transforms = Components(root, _b.transform);
	if (!transforms)
	{
		return transforms.GetError();
	}
	size_t moved = 0;
	for (size_t i = 0; i < _managed.ArrayLength(*transforms); ++i)
	{
		auto* transform = _managed.ArrayItem(*transforms, i);
		auto text = _managed.Invoke(_b.getName, transform);
		if (!text || *text == nullptr || _managed.Text(*text) != name)
		{
			continue;
		}
		if (auto set = _managed.Invoke(_b.setPosition, transform, {world->data()}); !set)
		{
			return set.GetError();
		}
		++moved;
	}
	return moved;
}

moa::Result<void> TemplateBuilder::SetScriptText(Il2CppObject* root, Il2CppClass* klass, size_t offset, const std::string& text)
{
	auto script = _managed.Invoke(_b.getComponent, root, {_managed.TypeObject(klass)});
	if (!script)
	{
		return script.GetError();
	}
	if (*script == nullptr)
	{
		return moa::MakeError("the root of the template has no script {}", _managed.Api().FullName(klass));
	}
	_managed.SetReferenceField(*script, offset, _managed.String(text.c_str()));
	return {};
}

moa::Result<void> TemplateBuilder::FinishKind(Il2CppObject* root, const PackResource& resource, const ResourceModel& model,
                                              nlohmann::json& report)
{
	if (resource.kind == PackKind::Resource)
	{
		return {};
	}
	if (!_b.kindErrors.empty())
	{
		return moa::MakeError("the mod cannot make a {} item: {}", PackListName(resource.kind), _b.kindErrors);
	}
	const auto point = ItemPoint(resource, model.mesh);
	if (resource.kind == PackKind::Throwable)
	{
		// The decoration finds its row by the name in its script, not by the name of the game object.
		if (auto set = SetScriptText(root, _b.decoration, _b.legacyName, resource.name); !set)
		{
			return set.GetError();
		}
		auto moved = MovePoints(root, k_HandGrabName, point);
		if (!moved)
		{
			return moved.GetError();
		}
		report["grips"] = *moved;
		report["grip"] = point;
		return {};
	}
	// A weapon part: the weapon finds the row of the part by the id in the block script.
	if (auto set = SetScriptText(root, _b.block, _b.blockInfoId, resource.name); !set)
	{
		return set.GetError();
	}
	auto moved = MovePoints(root, k_TipHingeName, point);
	if (!moved)
	{
		return moved.GetError();
	}
	if (*moved == 0)
	{
		return moa::MakeError("the template has no hinge {}", k_TipHingeName);
	}
	report["tip"] = point;
	return {};
}

moa::Result<void> TemplateBuilder::FitCollider(Il2CppObject* root, Il2CppObject* visual, Il2CppObject* mesh,
                                               const PackResource& resource, const moa::unitymesh::RuntimeMesh& runtimeMesh,
                                               nlohmann::json& report)
{
	auto box = _managed.Invoke(_b.getComponent, root, {_managed.TypeObject(_b.boxCollider)});
	if (!box)
	{
		return box.GetError();
	}
	switch (resource.collider)
	{
	case ColliderMode::Template:
		report["collider"] = "template";
		return {};
	case ColliderMode::Box:
	{
		if (*box == nullptr)
		{
			auto added = _managed.Invoke(_b.addComponent, root, {_managed.TypeObject(_b.boxCollider)});
			if (!added || *added == nullptr)
			{
				return moa::MakeError("the mod cannot add a BoxCollider");
			}
			*box = *added;
		}
		auto fit = FitBox(runtimeMesh, resource.scale);
		if (auto set = _managed.Invoke(_b.setBoxCentre, *box, {fit.centre.data()}); !set)
		{
			return set.GetError();
		}
		if (auto set = _managed.Invoke(_b.setBoxSize, *box, {fit.size.data()}); !set)
		{
			return set.GetError();
		}
		report["collider"] = {{"box", {{"centre", fit.centre}, {"size", fit.size}}}};
		return {};
	}
	case ColliderMode::Mesh:
	{
		// The collider goes on the visual, so that it gets the same scale as the mesh.
		auto added = _managed.Invoke(_b.addComponent, visual, {_managed.TypeObject(_b.meshCollider)});
		if (!added || *added == nullptr)
		{
			return moa::MakeError("the mod cannot add a MeshCollider");
		}
		// A body that moves needs a convex mesh collider.
		bool convex = true;
		if (auto set = _managed.Invoke(_b.setConvex, *added, {&convex}); !set)
		{
			return set.GetError();
		}
		if (auto set = _managed.Invoke(_b.setColliderMesh, *added, {mesh}); !set)
		{
			return set.GetError();
		}
		if (*box != nullptr)
		{
			bool enabled = false;
			(void)_managed.Invoke(_b.setColliderEnabled, *box, {&enabled});
		}
		report["collider"] = "mesh";
		return {};
	}
	}
	return {};
}

} // namespace moa_mod::objects
