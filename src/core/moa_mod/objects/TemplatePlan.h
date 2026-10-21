/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <array>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <moa/common/Result.h>
#include <moa/unitymesh/RuntimeMesh.h>
#include <moa_mod/objects/Pack.h>

// The parts of the template builder that do not need the game. The mod reads the files of a pack resource with these
// functions, and calculates the collider and the textures of the new prefab.

namespace moa_mod::objects
{

/// The PNG bytes of the textures of one material. An empty array tells that the material has no such texture.
struct MaterialImages
{
	std::string name;
	std::vector<std::byte> base;
	std::vector<std::byte> normal;
	std::vector<std::byte> mask;
};

/// The mesh and the textures of a pack resource, read from the pack folder.
struct ResourceModel
{
	moa::unitymesh::RuntimeMesh mesh;
	/// One item for each sub-mesh, or empty: then each sub-mesh uses the material of the template with no change.
	std::vector<MaterialImages> materials;
};

/// The largest vertex count that the mod accepts for a pack mesh.
constexpr size_t k_MaxPackMeshVertices = 1'000'000;

/// Read the bytes of a file of the pack. The path is relative to the pack folder, with "/" between the parts.
using PackFileReader = std::function<moa::Result<std::vector<std::byte>>(const std::string& path)>;

/// Read the mesh and the textures of a resource. Return an error if the resource has no mesh, if a file cannot be
/// read, if the mesh is not correct or too large, if an image is not a PNG file, or if the count of the materials is
/// not 0 and not the count of the sub-meshes.
[[nodiscard]] moa::Result<ResourceModel> LoadResourceModel(const PackResource& resource, const PackFileReader& read);

/// Return true if the bytes start with the signature of a PNG file.
[[nodiscard]] bool IsPng(std::span<const std::byte> bytes);

/// A box in the space of the prefab root.
struct BoxFit
{
	std::array<float, 3> centre {0.0f, 0.0f, 0.0f};
	std::array<float, 3> size {0.0f, 0.0f, 0.0f};
};

/// The smallest side of a box collider, so that a flat mesh still gets a box with a volume.
constexpr float k_MinColliderSide = 0.02f;

/// Return the box collider that fits the bounds of the mesh, multiplied by the scale of the resource.
[[nodiscard]] BoxFit FitBox(const moa::unitymesh::RuntimeMesh& mesh, float scale);

/// Return the point where the hand holds the object: the centre of the top of the bounds, multiplied by the scale.
[[nodiscard]] std::array<float, 3> GripPoint(const moa::unitymesh::RuntimeMesh& mesh, float scale);

/// Return the point of an item in the space of the prefab: the grip of a throwable or the tip of a weapon part. The
/// point of the pack (in metres, with the scale), else the centre of the top of the bounds, multiplied by the scale.
[[nodiscard]] std::array<float, 3> ItemPoint(const PackResource& item, const moa::unitymesh::RuntimeMesh& mesh);

/// The names of the children of the templates that the builder changes for each kind.
inline constexpr std::string_view k_HandGrabName = "HandGrab";  ///< A grip of the hand on a decoration.
inline constexpr std::string_view k_VisualsName = "Visuals";    ///< The parent of the visual parts of a block.
inline constexpr std::string_view k_TipHingeName = "Hinge_Top"; ///< The hinge at the point of a weapon part.

/// Change the indices of a sub-mesh to the signed 32-bit values of `Mesh.SetTriangles`. Return an error if an index
/// is not less than the vertex count, or if the count of the indices does not make full triangles.
[[nodiscard]] moa::Result<std::vector<int32_t>> ManagedIndices(std::span<const uint32_t> indices, size_t vertexCount);

/// A texture that the mod puts into the copy of the template material.
struct TextureSlot
{
	std::string_view property;      ///< The texture property of the common shader of the game.
	std::span<const std::byte> png; ///< The PNG bytes.
	bool linear = false;            ///< True for data (normal, mask); false for colour (sRGB).
};

/// Return the textures of a material that are not empty, in a fixed order: base colour, normal, mask.
[[nodiscard]] std::vector<TextureSlot> TextureSlots(const MaterialImages& images);

/// The texture properties of the template material that the mod clears, because they belong to the art of the
/// template: the district tint mask gets white, and the emission map gets black.
inline constexpr std::string_view k_TintMapProperty = "_TintMap";
inline constexpr std::string_view k_EmissionMapProperty = "_EmisiveMap";

/// The keywords of the common shader that the mod turns off in the copy of the template material. They add layers
/// (a detail overlay, a projection from the world) that use the textures of the template.
inline constexpr std::array<std::string_view, 2> k_KeywordsOff {"_DETAILOVERLAY", "_TRIPLANAR"};

} // namespace moa_mod::objects
