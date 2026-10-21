/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstddef>

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <moa/common/Result.h>
#include <moa_mod/objects/Pack.h>

namespace moa_mod::objects
{

/// The folder of the pickup prefabs in the Unity resources. The game loads `k_RawMaterialsFolder + m_prefab`.
inline constexpr std::string_view k_RawMaterialsFolder = "_Prefabs/RawMaterials/";

/// A pack resource that the merge added to the table.
struct MergedResource
{
	size_t pack = 0;     ///< The index of the pack in the span that the merge got.
	size_t resource = 0; ///< The index of the resource in its pack.
	std::string name;    ///< The m_name of the row.
	std::string id;      ///< The id of the row.
	/// The path that the game gives to `Resources.Load` for this resource: `k_RawMaterialsFolder + m_prefab`.
	std::string prefabPath;
	/// The path of the prefab of the template row. The mod clones this prefab.
	std::string templatePrefabPath;
	/// All the fields of the new row, in the order of the table.
	std::vector<std::pair<std::string, std::string>> row;
};

/// The result of a merge.
struct CarriableMerge
{
	std::string text;                   ///< The new text of the table.
	std::vector<MergedResource> merged; ///< In the order of the packs and of their resources.
	std::vector<PackError> errors;      ///< One for each pack that the merge did not add.
};

/// Add the carried resources of the packs (`PackKind::Resource`) to the text of the table `NGCarriableResources`.
///
/// The new text is the original text with the new rows before the last "]". All the other bytes stay the same,
/// also a UTF-8 byte order mark. A new row is a copy of its template row, in the field order of the template row,
/// with the id, m_name and m_prefab of the resource and the fields that the pack gives. Each value is a JSON string.
///
/// The merge adds all the resources of a pack, or none of them. A pack does not get added if one of its resources:
/// - has a name, a prefab or an id that a game row or a resource added before it has (names and prefabs ignore case),
/// - names a template that is not a game row, or a template that has no prefab (empty or "?").
///
/// Return an error only if the text is not a JSON array of objects. Then the mod must use the original text.
[[nodiscard]] moa::Result<CarriableMerge> MergeCarriableRows(std::string_view tableText, std::span<const Pack> packs);

/// Find the merged resource that a `Resources.Load` path asks for. The comparison ignores the case of ASCII letters
/// and treats "\" as "/". Return nothing if no merged resource has this prefab path.
[[nodiscard]] const MergedResource* MatchResourcePath(std::string_view requestedPath, std::span<const MergedResource> merged);

} // namespace moa_mod::objects
