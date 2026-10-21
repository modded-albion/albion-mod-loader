/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "moa_mod/objects/TemplatePlan.h"

#include <algorithm>
#include <limits>

namespace moa_mod::objects
{
namespace
{

constexpr std::array<uint8_t, 8> k_PngSignature {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

/// Read one PNG file of a material. An absent path gives an empty array.
[[nodiscard]] moa::Result<std::vector<std::byte>> ReadImage(const std::optional<std::string>& path, const PackFileReader& read)
{
	if (!path)
	{
		return std::vector<std::byte> {};
	}
	auto bytes = read(*path);
	if (!bytes)
	{
		return moa::MakeError("{}: {}", *path, bytes.GetError().message);
	}
	if (!IsPng(*bytes))
	{
		return moa::MakeError("{}: the file is not a PNG image", *path);
	}
	return std::move(*bytes);
}

} // namespace

bool IsPng(std::span<const std::byte> bytes)
{
	return bytes.size() >= k_PngSignature.size() &&
	       std::ranges::equal(bytes.first(k_PngSignature.size()), k_PngSignature,
	                          [](std::byte a, uint8_t b) { return std::to_integer<uint8_t>(a) == b; });
}

moa::Result<ResourceModel> LoadResourceModel(const PackResource& resource, const PackFileReader& read)
{
	if (!resource.mesh)
	{
		return moa::MakeError("the resource {} has no mesh", resource.name);
	}
	auto bytes = read(*resource.mesh);
	if (!bytes)
	{
		return moa::MakeError("{}: {}", *resource.mesh, bytes.GetError().message);
	}
	auto mesh = moa::unitymesh::ReadRuntimeMesh(*bytes);
	if (!mesh)
	{
		return moa::MakeError("{}: {}", *resource.mesh, mesh.GetError().message);
	}
	if (mesh->vertices.size() > k_MaxPackMeshVertices)
	{
		return moa::MakeError("{}: the mesh has {} vertices; the mod accepts not more than {}", *resource.mesh,
		                      mesh->vertices.size(), k_MaxPackMeshVertices);
	}
	if (mesh->subMeshTriangles.empty())
	{
		return moa::MakeError("{}: the mesh has no sub-mesh", *resource.mesh);
	}
	const auto subMeshes = mesh->subMeshTriangles.size();
	if (!resource.materials.empty() && resource.materials.size() != subMeshes)
	{
		return moa::MakeError("the resource {} has {} materials, but its mesh has {} sub-meshes", resource.name,
		                      resource.materials.size(), subMeshes);
	}
	ResourceModel model {.mesh = std::move(*mesh), .materials = {}};
	for (const auto& material : resource.materials)
	{
		MaterialImages images {.name = material.name, .base = {}, .normal = {}, .mask = {}};
		for (const auto& [path, target] :
		     {std::pair {&material.base, &images.base}, std::pair {&material.normal, &images.normal},
		      std::pair {&material.mask, &images.mask}})
		{
			auto image = ReadImage(*path, read);
			if (!image)
			{
				return image.GetError();
			}
			*target = std::move(*image);
		}
		model.materials.push_back(std::move(images));
	}
	return model;
}

BoxFit FitBox(const moa::unitymesh::RuntimeMesh& mesh, float scale)
{
	BoxFit box;
	for (size_t i = 0; i < 3; ++i)
	{
		box.centre[i] = mesh.boundsCentre[i] * scale;
		box.size[i] = std::max(mesh.boundsExtents[i] * 2.0f * scale, k_MinColliderSide);
	}
	return box;
}

std::array<float, 3> GripPoint(const moa::unitymesh::RuntimeMesh& mesh, float scale)
{
	return {mesh.boundsCentre[0] * scale, (mesh.boundsCentre[1] + mesh.boundsExtents[1]) * scale, mesh.boundsCentre[2] * scale};
}

std::array<float, 3> ItemPoint(const PackResource& item, const moa::unitymesh::RuntimeMesh& mesh)
{
	if (item.grip)
	{
		return *item.grip;
	}
	return GripPoint(mesh, item.scale);
}

moa::Result<std::vector<int32_t>> ManagedIndices(std::span<const uint32_t> indices, size_t vertexCount)
{
	if (indices.size() % 3 != 0)
	{
		return moa::MakeError("{} indices do not make full triangles", indices.size());
	}
	if (vertexCount > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
	{
		return moa::MakeError("the mesh has too many vertices for 32-bit indices");
	}
	std::vector<int32_t> out;
	out.reserve(indices.size());
	for (const auto index : indices)
	{
		if (index >= vertexCount)
		{
			return moa::MakeError("the index {} is not less than the vertex count {}", index, vertexCount);
		}
		out.push_back(static_cast<int32_t>(index));
	}
	return out;
}

std::vector<TextureSlot> TextureSlots(const MaterialImages& images)
{
	std::vector<TextureSlot> slots;
	for (const auto& slot : {TextureSlot {.property = "_BaseMap", .png = images.base, .linear = false},
	                         TextureSlot {.property = "_BumpMap", .png = images.normal, .linear = true},
	                         TextureSlot {.property = "_MaskMap", .png = images.mask, .linear = true}})
	{
		if (!slot.png.empty())
		{
			slots.push_back(slot);
		}
	}
	return slots;
}

} // namespace moa_mod::objects
