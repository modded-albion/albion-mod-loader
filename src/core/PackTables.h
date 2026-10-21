/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <core/CarriableMerge.h>
#include <core/Pack.h>
#include <core/TypedMerge.h>

namespace moa_mod::objects
{

/// The tables of the game that the packs can change.
enum class PackTable : uint8_t
{
	Carriable,  ///< `NGCarriableResources`: the carried resources.
	Decoration, ///< `NGDecorationInfo`: the throwables.
	Block,      ///< `NGBlockInfo`: the weapon parts.
	HandPower,  ///< `MAHandPowerInfo`: the levels of the spells.
	Research,   ///< `MAResearchInfo`: the research rows that unlock the levels of the spells.
};

/// The resource path of each table, in lower case, as `NormalResourcePath` gives it.
inline constexpr std::array<std::pair<PackTable, std::string_view>, 5> k_PackTablePaths {{
    {PackTable::Carriable, "reactxls/ngbalance/ngknack/ngcarriableresources"},
    {PackTable::Decoration, "reactxls/ngbalance/ngknack/ngdecorationinfo"},
    {PackTable::Block, "reactxls/ngbalance/ngknack/ngblockinfo"},
    {PackTable::HandPower, "reactxls/ngbalance/ngknack/mahandpowerinfo"},
    {PackTable::Research, "reactxls/ngbalance/ngknack/maresearchinfo"},
}};

/// Return the table of a normal resource path, or nothing.
[[nodiscard]] std::optional<PackTable> PackTableOfPath(std::string_view normalPath);

/// Return the resource path of a table.
[[nodiscard]] std::string_view PackTablePath(PackTable table);

/// Return true if an item of the packs changes the table.
[[nodiscard]] bool PacksChangeTable(std::span<const Pack> packs, PackTable table);

/// The merges of the tables for the mod. The mod gives the text of a table when the game loads it, and gets the new
/// text. The merger keeps the items with prefabs, so that the mod can serve their prefab paths, and the errors of the
/// packs. It has no state of the game.
class PackTableMerger
{
public:
	/// Read the original text of a table, or nothing if it cannot. The merger asks for the other table of the spells.
	using TextLoader = std::function<std::optional<std::string>(PackTable table)>;

	explicit PackTableMerger(std::vector<Pack> packs);

	/// Return the new text of a table, or nothing if the packs do not change it or the text is not a JSON table (then
	/// the game must use its own text, and `TakeErrors` tells why).
	[[nodiscard]] std::optional<std::string> Merge(PackTable table, std::string_view text, const TextLoader& loadOriginal);

	/// The items with a prefab that the last merges added: the carried resources, the throwables and the weapon parts.
	[[nodiscard]] std::vector<MergedResource> Merged() const;

	/// The items with a prefab that the last merge of one table added.
	[[nodiscard]] std::vector<MergedResource> MergedIn(PackTable table) const;

	/// The new levels of the spells that the last merge of the spells added.
	[[nodiscard]] const std::vector<SpellLevel>& Spells() const { return _spells; }

	/// The errors since the last call: a pack that a merge did not add (the pack id), or a table that did not merge.
	[[nodiscard]] std::vector<PackError> TakeErrors();

	[[nodiscard]] const std::vector<Pack>& Packs() const { return _packs; }

private:
	std::vector<Pack> _packs;
	std::map<PackTable, std::vector<MergedResource>> _merged;
	std::vector<SpellLevel> _spells;
	/// The input texts of the last merge of the spells, by table, so that a new text of one table merges again.
	std::map<PackTable, std::string> _spellInputs;
	std::map<PackTable, std::string> _spellOutputs;
	std::vector<PackError> _errors;
};

} // namespace moa_mod::objects
