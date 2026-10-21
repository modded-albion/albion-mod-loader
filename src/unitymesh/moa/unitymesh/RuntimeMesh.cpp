/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "moa/unitymesh/RuntimeMesh.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

#include <moa/common/BinaryReader.h>

namespace moa::unitymesh
{
namespace
{

constexpr uint32_t k_FlagIndex32 = 1u << 0;
constexpr uint32_t k_FlagNormals = 1u << 1;
constexpr uint32_t k_FlagTangents = 1u << 2;
constexpr uint32_t k_FlagUv0 = 1u << 3;
constexpr uint32_t k_FlagUv1 = 1u << 4;
constexpr uint32_t k_FlagColours = 1u << 5;
constexpr uint32_t k_FlagsKnown = (1u << 6) - 1;

/// Append values to a byte array in little-endian order.
class ByteWriter
{
public:
	void U32(uint32_t value)
	{
		for (int shift = 0; shift < 32; shift += 8)
		{
			_bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
		}
	}

	void I32(int32_t value) { U32(std::bit_cast<uint32_t>(value)); }
	void F32(float value) { U32(std::bit_cast<uint32_t>(value)); }

	template <size_t N>
	void Floats(const std::vector<std::array<float, N>>& values)
	{
		for (const auto& value : values)
		{
			for (const float item : value)
			{
				F32(item);
			}
		}
	}

	void Bytes(std::span<const std::byte> bytes) { _bytes.insert(_bytes.end(), bytes.begin(), bytes.end()); }

	[[nodiscard]] std::vector<std::byte> Take() { return std::move(_bytes); }

private:
	std::vector<std::byte> _bytes;
};

/// Read N floats for each vertex. Check the size against the data before the allocation.
template <size_t N>
[[nodiscard]] bool ReadFloats(BinaryReader& reader, size_t count, std::vector<std::array<float, N>>& out)
{
	if (reader.Remaining() / (N * sizeof(float)) < count)
	{
		return false;
	}
	out.resize(count);
	for (auto& value : out)
	{
		for (float& item : value)
		{
			item = reader.Read<float>();
		}
	}
	return !reader.Failed();
}

} // namespace

Result<void> CheckRuntimeMesh(const RuntimeMesh& mesh)
{
	const auto count = mesh.vertices.size();
	if (count == 0)
	{
		return MakeError("the mesh has no vertices");
	}
	if (count > std::numeric_limits<uint32_t>::max())
	{
		return MakeError("the mesh has too many vertices");
	}
	const auto sameSize = [count](const auto& values) { return values.empty() || values.size() == count; };
	if (!sameSize(mesh.normals) || !sameSize(mesh.tangents) || !sameSize(mesh.uv0) || !sameSize(mesh.uv1) ||
	    !sameSize(mesh.colours))
	{
		return MakeError("a vertex array does not have {} items", count);
	}
	if (!mesh.index32 && count > k_MaxIndex16Vertices)
	{
		return MakeError("{} vertices need 32-bit indices", count);
	}
	if (mesh.subMeshMaterials.size() != mesh.subMeshTriangles.size())
	{
		return MakeError("the mesh has {} sub-meshes but {} materials", mesh.subMeshTriangles.size(),
		                 mesh.subMeshMaterials.size());
	}
	for (size_t i = 0; i < mesh.subMeshTriangles.size(); ++i)
	{
		const auto& triangles = mesh.subMeshTriangles[i];
		if (triangles.size() % 3 != 0)
		{
			return MakeError("the sub-mesh {} has {} indices, which do not make full triangles", i, triangles.size());
		}
		for (const auto index : triangles)
		{
			if (index >= count)
			{
				return MakeError("the sub-mesh {} has the index {}, but the mesh has {} vertices", i, index, count);
			}
		}
		if (mesh.subMeshMaterials[i] < -1)
		{
			return MakeError("the sub-mesh {} has the material {}", i, mesh.subMeshMaterials[i]);
		}
	}
	return {};
}

Result<std::vector<std::byte>> WriteRuntimeMesh(const RuntimeMesh& mesh)
{
	if (auto check = CheckRuntimeMesh(mesh); !check)
	{
		return check.GetError();
	}
	uint32_t flags = 0;
	flags |= mesh.index32 ? k_FlagIndex32 : 0u;
	flags |= mesh.normals.empty() ? 0u : k_FlagNormals;
	flags |= mesh.tangents.empty() ? 0u : k_FlagTangents;
	flags |= mesh.uv0.empty() ? 0u : k_FlagUv0;
	flags |= mesh.uv1.empty() ? 0u : k_FlagUv1;
	flags |= mesh.colours.empty() ? 0u : k_FlagColours;

	ByteWriter writer;
	writer.Bytes(std::as_bytes(std::span(k_RuntimeMeshMagic)));
	writer.U32(k_RuntimeMeshVersion);
	writer.U32(flags);
	writer.U32(static_cast<uint32_t>(mesh.vertices.size()));
	writer.U32(static_cast<uint32_t>(mesh.subMeshTriangles.size()));
	for (const float value : mesh.boundsCentre)
	{
		writer.F32(value);
	}
	for (const float value : mesh.boundsExtents)
	{
		writer.F32(value);
	}
	writer.U32(static_cast<uint32_t>(mesh.name.size()));
	writer.Bytes(std::as_bytes(std::span(mesh.name)));
	writer.Floats(mesh.vertices);
	writer.Floats(mesh.normals);
	writer.Floats(mesh.tangents);
	writer.Floats(mesh.uv0);
	writer.Floats(mesh.uv1);
	writer.Floats(mesh.colours);
	for (size_t i = 0; i < mesh.subMeshTriangles.size(); ++i)
	{
		writer.I32(mesh.subMeshMaterials[i]);
		writer.U32(static_cast<uint32_t>(mesh.subMeshTriangles[i].size()));
		for (const auto index : mesh.subMeshTriangles[i])
		{
			writer.U32(index);
		}
	}
	return writer.Take();
}

Result<RuntimeMesh> ReadRuntimeMesh(std::span<const std::byte> data)
{
	BinaryReader reader(data, Endian::Little);
	const auto magic = reader.ReadBytes(k_RuntimeMeshMagic.size());
	if (reader.Failed() || !std::ranges::equal(magic, std::as_bytes(std::span(k_RuntimeMeshMagic))))
	{
		return MakeError("the data is not a runtime mesh");
	}
	const auto version = reader.Read<uint32_t>();
	if (version != k_RuntimeMeshVersion)
	{
		return MakeError("the runtime mesh version {} is not known", version);
	}
	const auto flags = reader.Read<uint32_t>();
	if ((flags & ~k_FlagsKnown) != 0)
	{
		return MakeError("the runtime mesh has flags that are not known ({:#x})", flags);
	}
	const auto vertexCount = reader.Read<uint32_t>();
	const auto subMeshCount = reader.Read<uint32_t>();

	RuntimeMesh mesh;
	mesh.index32 = (flags & k_FlagIndex32) != 0;
	for (float& value : mesh.boundsCentre)
	{
		value = reader.Read<float>();
	}
	for (float& value : mesh.boundsExtents)
	{
		value = reader.Read<float>();
	}
	const auto nameLength = reader.Read<uint32_t>();
	const auto name = reader.ReadBytes(nameLength);
	if (reader.Failed())
	{
		return MakeError("the runtime mesh ends in its header");
	}
	mesh.name.assign(reinterpret_cast<const char*>(name.data()), name.size());

	const auto readArray = [&](uint32_t flag, auto& out) {
		return (flags & flag) == 0 || ReadFloats(reader, vertexCount, out);
	};
	if (!ReadFloats(reader, vertexCount, mesh.vertices) || !readArray(k_FlagNormals, mesh.normals) ||
	    !readArray(k_FlagTangents, mesh.tangents) || !readArray(k_FlagUv0, mesh.uv0) || !readArray(k_FlagUv1, mesh.uv1) ||
	    !readArray(k_FlagColours, mesh.colours))
	{
		return MakeError("the runtime mesh ends in its vertex arrays");
	}

	// A sub-mesh has at least 8 bytes, so a count larger than the data is not correct.
	if (reader.Remaining() / 8 < subMeshCount)
	{
		return MakeError("the runtime mesh ends in its sub-meshes");
	}
	mesh.subMeshTriangles.resize(subMeshCount);
	mesh.subMeshMaterials.resize(subMeshCount);
	for (uint32_t i = 0; i < subMeshCount; ++i)
	{
		mesh.subMeshMaterials[i] = reader.Read<int32_t>();
		const auto indexCount = reader.Read<uint32_t>();
		if (reader.Failed() || reader.Remaining() / sizeof(uint32_t) < indexCount)
		{
			return MakeError("the runtime mesh ends in the sub-mesh {}", i);
		}
		auto& triangles = mesh.subMeshTriangles[i];
		triangles.resize(indexCount);
		for (auto& index : triangles)
		{
			index = reader.Read<uint32_t>();
		}
	}
	if (reader.Failed())
	{
		return MakeError("the runtime mesh ends too early");
	}
	if (reader.Remaining() != 0)
	{
		return MakeError("the runtime mesh has {} bytes after its end", reader.Remaining());
	}
	if (auto check = CheckRuntimeMesh(mesh); !check)
	{
		return check.GetError();
	}
	return mesh;
}

} // namespace moa::unitymesh
