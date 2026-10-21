/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstddef>

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <moa/common/Result.h>
#include <moa_mod/objects/CarriableMerge.h>
#include <moa_mod/objects/Pack.h>

// The merges of the typed items of the packs (format 2) into the tables of the game. Each merge keeps the original
// text of the table and adds rows before the last "]" (see `MergeKnackRows`). A merge adds all the items of one kind
// of a pack, or none of them.

namespace moa_mod::objects
{

/// The folder of the prefabs of the throwables of the packs. The game loads `m_location + "/" + m_prefabName`, and the
/// mod gives its copy of the template for that path.
inline constexpr std::string_view k_PackDecorationFolder = "_Art/Decorations/MOAR";

/// The folder of the block prefabs in the Unity resources. The game loads `k_BlocksFolder + m_prefabPath`.
inline constexpr std::string_view k_BlocksFolder = "_Prefabs/_Blocks/";

/// The first part of `m_prefabPath` of a weapon part of a pack: `MOAR/<pack id>/<name>`.
inline constexpr std::string_view k_PackBlockFolder = "MOAR";

/// The product line of the weapons. A weapon part of a pack must copy a block of this line.
inline constexpr std::string_view k_WeaponProductLine = "Weapons";

/// The result of the merge of the throwables or of the weapon parts.
struct TypedMerge
{
	std::string text;                   ///< The new text of the table.
	std::vector<MergedResource> merged; ///< In the order of the packs and of their items.
	std::vector<PackError> errors;      ///< One for each pack whose items of the kind the merge did not add.
};

/// Add the throwables of the packs to the text of the table `NGDecorationInfo`. A new row is a copy of the row of its
/// template, with the id, `m_name`, `m_prefabName` (the prefab of the item), `m_location` (`k_PackDecorationFolder`),
/// `m_type` "Throwable" and the typed values. Return an error only if the text is not a JSON array of objects.
[[nodiscard]] moa::Result<TypedMerge> MergeThrowableRows(std::string_view tableText, std::span<const Pack> packs);

/// Add the weapon parts of the packs to the text of the table `NGBlockInfo`. A new row is a copy of the row of its
/// template (a block of the product line `Weapons`), with the id, `m_prefabName` (the name of the item, which designs
/// use), `m_prefabPath` (`MOAR/<pack id>/<name>`), `m_tooltip` (the title) and the typed values. Return an error only if
/// the text is not a JSON array of objects.
[[nodiscard]] moa::Result<TypedMerge> MergeWeaponRows(std::string_view tableText, std::span<const Pack> packs);

/// A new level of a hand power that the spell merge added.
struct SpellLevel
{
	size_t pack = 0;     ///< The index of the pack in the span that the merge got.
	size_t resource = 0; ///< The index of the spell in its pack.
	std::string power;   ///< The name of the power, for example "Fireball".
	int level = 0;
	std::string researchKey; ///< The `m_key` of the research row that unlocks the level.
	std::string unlock;      ///< The `m_unlock` of that row, for example "m_handPowerFireballLevel=6".
};

struct SpellMerge
{
	std::string handPowers; ///< The new text of `MAHandPowerInfo`.
	std::string research;   ///< The new text of `MAResearchInfo`.
	std::vector<SpellLevel> added;
	std::vector<PackError> errors; ///< One for each pack whose spells the merge did not add.
};

/// Add the spells of the packs: for each new level of a power, a row of `MAHandPowerInfo` and a row of
/// `MAResearchInfo` that unlocks the level, linked from the research row of the level before it.
///
/// - A new level must be the next level of its power: the highest level of the game plus one, or the level that a
///   spell before it added plus one. The levels of a pack can come in any order.
/// - The hand power row copies the game row of `templateLevel` (default: the highest level of the game).
/// - The research row copies the game row that unlocks the highest level of the game: its key, name and unlock get
///   the new level, its place in the tree moves one step further in the direction of the last two levels, and its
///   link is empty. The row before it gets a link to it: the merge changes only the bytes of that link.
///
/// Return an error only if a text is not a JSON array of objects.
[[nodiscard]] moa::Result<SpellMerge> MergeSpellRows(std::string_view handPowerTable, std::string_view researchTable,
                                                     std::span<const Pack> packs);

} // namespace moa_mod::objects
