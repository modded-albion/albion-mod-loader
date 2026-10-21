/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <array>
#include <span>
#include <string>
#include <vector>

#include <moa/common/Result.h>

// This header and RuntimeMesh.cpp use only the standard library and moa_common. The research mod compiles
// RuntimeMesh.cpp itself, because the mod uses the static C runtime and cannot link the component libraries.

namespace moa::unitymesh
{

/// The arrays that the `Mesh` API of Unity takes, in the space of the game (left-handed, Y up, texture coordinate
/// (0, 0) at the bottom left, clockwise front faces). Every vertex array that is not empty has one item for each
/// vertex.
struct RuntimeMesh
{
	std::string name;
	std::vector<std::array<float, 3>> vertices;
	std::vector<std::array<float, 3>> normals;
	std::vector<std::array<float, 4>> tangents; ///< xyz and the sign of the bitangent in w (1 or -1).
	std::vector<std::array<float, 2>> uv0;
	std::vector<std::array<float, 2>> uv1;
	std::vector<std::array<float, 4>> colours; ///< RGBA from 0 to 1, for `Mesh.colors`.
	/// True: the mesh needs 32-bit indices (`IndexFormat.UInt32`). False: 16-bit indices are sufficient.
	bool index32 = false;
	/// Three indices for each triangle, one list for each sub-mesh.
	std::vector<std::vector<uint32_t>> subMeshTriangles;
	/// The material of each sub-mesh: an index into the materials of the source model, or -1 for no material.
	std::vector<int32_t> subMeshMaterials;
	std::array<float, 3> boundsCentre {0.0f, 0.0f, 0.0f};
	std::array<float, 3> boundsExtents {0.0f, 0.0f, 0.0f};
};

/// The largest vertex count that 16-bit indices can address.
constexpr size_t k_MaxIndex16Vertices = 65535;

/// Check that the arrays agree: the vertex arrays have the same size, the indices are in range and make full
/// triangles, each sub-mesh has a material entry, and 16-bit indices address all the vertices.
[[nodiscard]] Result<void> CheckRuntimeMesh(const RuntimeMesh& mesh);

// The binary form of a runtime mesh. All the values are little-endian; floats are IEEE 754 binary32.
//
//   8 bytes   the text "MOARMESH"
//   u32       the version (1)
//   u32       the flags: 1 index32, 2 normals, 4 tangents, 8 uv0, 16 uv1, 32 colours
//   u32       the vertex count V
//   u32       the sub-mesh count S
//   f32 x 3   the centre of the bounds
//   f32 x 3   the extents of the bounds
//   u32       the length N of the name, then N bytes of UTF-8 text
//   f32 x 3V  the vertices
//   f32 x 3V  the normals (when the flag is set)
//   f32 x 4V  the tangents (when the flag is set)
//   f32 x 2V  uv0 (when the flag is set)
//   f32 x 2V  uv1 (when the flag is set)
//   f32 x 4V  the colours (when the flag is set)
//   S times:  i32 the material, u32 the index count I, then u32 x I indices
//
// No bytes follow the last sub-mesh.

constexpr std::array<char, 8> k_RuntimeMeshMagic {'M', 'O', 'A', 'R', 'M', 'E', 'S', 'H'};
constexpr uint32_t k_RuntimeMeshVersion = 1;

/// Write a runtime mesh in the binary form. Return an error if `CheckRuntimeMesh` refuses the mesh.
[[nodiscard]] Result<std::vector<std::byte>> WriteRuntimeMesh(const RuntimeMesh& mesh);

/// Read a runtime mesh from the binary form. Return an error if the data is not correct; never read out of the data.
[[nodiscard]] Result<RuntimeMesh> ReadRuntimeMesh(std::span<const std::byte> data);

} // namespace moa::unitymesh
