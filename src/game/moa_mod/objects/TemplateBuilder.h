/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <array>
#include <functional>
#include <string>

#include <moa/common/Result.h>
#include <moa_mod/objects/Pack.h>
#include <moa_mod/objects/TemplatePlan.h>
#include <nlohmann/json.hpp>

#include "moa_mod/Managed.h"

namespace moa_mod::objects
{

/// The names of Unity that the template builder uses. A missing name stops only the custom models; the copies of the
/// templates still work.
struct TemplateBindings
{
	Il2CppClass* vector2 = nullptr;
	Il2CppClass* vector3 = nullptr;
	Il2CppClass* vector4 = nullptr;
	Il2CppClass* colour = nullptr;
	Il2CppClass* int32 = nullptr;
	Il2CppClass* byte = nullptr;
	Il2CppClass* gameObject = nullptr;
	Il2CppClass* mesh = nullptr;
	Il2CppClass* material = nullptr;
	Il2CppClass* texture2d = nullptr;
	Il2CppClass* renderer = nullptr;
	Il2CppClass* meshFilter = nullptr;
	Il2CppClass* meshRenderer = nullptr;
	Il2CppClass* lodGroup = nullptr;
	Il2CppClass* boxCollider = nullptr;
	Il2CppClass* meshCollider = nullptr;
	Il2CppClass* batchInfoHolder = nullptr; ///< A script of the game on the renderers. Null if the game has none.

	const MethodInfo* meshMake = nullptr;
	const MethodInfo* setIndexFormat = nullptr;
	const MethodInfo* setVertices = nullptr;
	const MethodInfo* setNormals = nullptr;
	const MethodInfo* setTangents = nullptr;
	const MethodInfo* setUv = nullptr;
	const MethodInfo* setUv2 = nullptr;
	const MethodInfo* setColours = nullptr;
	const MethodInfo* setSubMeshCount = nullptr;
	const MethodInfo* setTriangles = nullptr;
	const MethodInfo* setMeshBounds = nullptr;
	const MethodInfo* uploadMeshData = nullptr;

	const MethodInfo* materialCopy = nullptr; ///< The constructor of a material from a material.
	const MethodInfo* setTexture = nullptr;
	const MethodInfo* hasProperty = nullptr;
	const MethodInfo* disableKeyword = nullptr;
	const MethodInfo* textureMake = nullptr; ///< The constructor of a texture with a size, a format, mipmaps and linear.
	const MethodInfo* loadImage = nullptr;
	const MethodInfo* whiteTexture = nullptr;
	const MethodInfo* blackTexture = nullptr;

	const MethodInfo* gameObjectMake = nullptr;
	const MethodInfo* getComponentsInChildren = nullptr;
	const MethodInfo* getComponent = nullptr;
	const MethodInfo* addComponent = nullptr;
	const MethodInfo* gameObjectTransform = nullptr;
	const MethodInfo* getLayer = nullptr;
	const MethodInfo* setLayer = nullptr;
	const MethodInfo* componentGameObject = nullptr;
	const MethodInfo* setParent = nullptr;
	const MethodInfo* setLocalPosition = nullptr;
	const MethodInfo* setLocalScale = nullptr;
	const MethodInfo* destroyImmediate = nullptr;
	const MethodInfo* setName = nullptr;

	const MethodInfo* getSharedMaterial = nullptr;
	const MethodInfo* setSharedMaterial = nullptr;
	const MethodInfo* setSharedMaterials = nullptr;
	const MethodInfo* setSharedMesh = nullptr;
	const MethodInfo* setBoxCentre = nullptr;
	const MethodInfo* setBoxSize = nullptr;
	const MethodInfo* setColliderEnabled = nullptr;
	const MethodInfo* setColliderMesh = nullptr;
	const MethodInfo* setConvex = nullptr;

	/// The names that only the typed items (throwables and weapon parts) need. If one is missing, `kindErrors` tells
	/// which, and the builder refuses only these items.
	Il2CppClass* transform = nullptr;
	Il2CppClass* collider = nullptr;
	Il2CppClass* decoration = nullptr;       ///< `NGDecoration`, the script of a decoration.
	Il2CppClass* block = nullptr;            ///< `Block`, the script of a building block or a weapon part.
	Il2CppClass* navBlocker = nullptr;       ///< `BuildingNavBlocker`. Optional.
	Il2CppClass* cylinderCollider = nullptr; ///< `CylinderCollider`, a collider script of the game. Optional.
	const MethodInfo* getName = nullptr;
	const MethodInfo* transformPoint = nullptr;
	const MethodInfo* setPosition = nullptr;
	const MethodInfo* getIsTrigger = nullptr;
	size_t legacyName = 0;  ///< The field `NGLegacyBase.m_name`: the decoration finds its table row by it.
	size_t blockInfoId = 0; ///< The field `Block.m_blockInfoID`: the weapon finds the row of the part by it.
	std::string kindErrors;

	[[nodiscard]] static moa::Result<TemplateBindings> Bind(const Managed& managed);
};

/// Change the copy of a template prefab so that it shows the mesh and the textures of a pack resource:
///
/// 1. Remove the renderers, the mesh filters and the levels of detail of the template.
/// 2. Add the child "Visual" with a new mesh and copies of the template material with the textures of the pack.
/// 3. Fit the collider of the root to the bounds of the mesh, as the pack tells.
///
/// Call it only on the main thread. The builder does not keep the objects; the caller keeps them alive.
class TemplateBuilder
{
public:
	/// The function that the builder calls for each new Unity object, so that the caller can keep it.
	using KeepFunction = std::function<void(Il2CppObject*)>;

	TemplateBuilder(const Managed& managed, TemplateBindings bindings, KeepFunction keep);

	/// Change the prefab `root` (a GameObject). Return a description of what the builder made.
	[[nodiscard]] moa::Result<nlohmann::json> Build(Il2CppObject* root, const PackResource& resource,
	                                                const ResourceModel& model);

private:
	[[nodiscard]] moa::Result<Il2CppObject*> Components(Il2CppObject* gameObject, Il2CppClass* klass);
	[[nodiscard]] moa::Result<Il2CppObject*> MakeMesh(const moa::unitymesh::RuntimeMesh& mesh, const std::string& name);
	[[nodiscard]] moa::Result<Il2CppObject*> MakeMaterial(Il2CppObject* source, const MaterialImages& images,
	                                                      const std::string& name);
	[[nodiscard]] moa::Result<Il2CppObject*> MakeTexture(const TextureSlot& slot, const std::string& name);
	[[nodiscard]] moa::Result<void> FitCollider(Il2CppObject* root, Il2CppObject* visual, Il2CppObject* mesh,
	                                            const PackResource& resource, const moa::unitymesh::RuntimeMesh& runtimeMesh,
	                                            nlohmann::json& report);
	/// Return the first transform in the copy with this name, or null.
	[[nodiscard]] moa::Result<Il2CppObject*> FindTransform(Il2CppObject* root, std::string_view name);
	/// Turn off the colliders of the template that are not triggers and not on a navigation blocker, and remove the
	/// collider scripts of the game, so that only the new collider gives the shape.
	[[nodiscard]] moa::Result<size_t> DisableTemplateColliders(Il2CppObject* root);
	/// Move each child with this name to a point in the space of the root. Return the count of children that moved.
	[[nodiscard]] moa::Result<size_t> MovePoints(Il2CppObject* root, std::string_view name, const std::array<float, 3>& point);
	/// Write a text field of the first script of a class in the copy.
	[[nodiscard]] moa::Result<void> SetScriptText(Il2CppObject* root, Il2CppClass* klass, size_t offset,
	                                              const std::string& text);
	/// The steps of a throwable or a weapon part after the new visual is in place.
	[[nodiscard]] moa::Result<void> FinishKind(Il2CppObject* root, const PackResource& resource, const ResourceModel& model,
	                                           nlohmann::json& report);

	const Managed& _managed;
	TemplateBindings _b;
	KeepFunction _keep;
};

} // namespace moa_mod::objects
