/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "moa_mod/objects/TypedMerge.h"

#include <charconv>
#include <cmath>

#include <algorithm>
#include <format>
#include <map>
#include <optional>

#include <nlohmann/json.hpp>

#include "moa_mod/objects/KnackMerge.h"

namespace moa_mod::objects
{
namespace
{

using OrderedJson = nlohmann::ordered_json;

constexpr std::string_view k_ByteOrderMark = "\xEF\xBB\xBF";

/// The rows of a table as JSON objects, or an error if the text is not a JSON array of objects.
[[nodiscard]] moa::Result<OrderedJson> ParseTable(std::string_view text)
{
	if (text.starts_with(k_ByteOrderMark))
	{
		text.remove_prefix(k_ByteOrderMark.size());
	}
	auto table = OrderedJson::parse(text, nullptr, false);
	if (table.is_discarded() || !table.is_array())
	{
		return moa::MakeError("the table is not a JSON array");
	}
	for (size_t i = 0; i < table.size(); ++i)
	{
		if (!table[i].is_object())
		{
			return moa::MakeError("the item {} of the table is not an object", i);
		}
	}
	return table;
}

/// The text of a field of a row: a JSON text without quotes, or the JSON text of another value.
[[nodiscard]] std::string Text(const OrderedJson& row, std::string_view key)
{
	const auto found = row.find(std::string(key));
	if (found == row.end())
	{
		return {};
	}
	return found->is_string() ? found->get<std::string>() : found->dump();
}

/// Find the row whose field `key` has the value `value` (the case of ASCII letters is ignored).
[[nodiscard]] const OrderedJson* FindRow(const OrderedJson& table, std::string_view key, std::string_view value)
{
	for (const auto& row : table)
	{
		if (EqualsIgnoreCase(Text(row, key), value))
		{
			return &row;
		}
	}
	return nullptr;
}

/// The typed values of an item as the values of a Knack row request: the field of each value gets the value.
void AddTypedValues(const PackResource& item, KnackRowRequest& request)
{
	for (const auto& [key, value] : item.values)
	{
		const auto* spec = FindPackField(item.kind, key);
		if (spec != nullptr && !spec->field.empty())
		{
			request.values.emplace_back(std::string(spec->field), value);
		}
	}
}

/// The items of a kind of each pack, as the rows of a Knack merge. `make` makes the request of one item.
template <typename Make>
[[nodiscard]] std::vector<KnackPackRows> CollectRows(std::span<const Pack> packs, PackKind kind, Make&& make,
                                                     std::vector<std::vector<size_t>>& itemIndices)
{
	std::vector<KnackPackRows> rows;
	itemIndices.clear();
	for (const auto& pack : packs)
	{
		KnackPackRows packRows {.pack = pack.id, .rows = {}};
		std::vector<size_t> indices;
		for (size_t i = 0; i < pack.resources.size(); ++i)
		{
			if (pack.resources[i].kind == kind)
			{
				packRows.rows.push_back(make(pack, pack.resources[i]));
				indices.push_back(i);
			}
		}
		rows.push_back(std::move(packRows));
		itemIndices.push_back(std::move(indices));
	}
	return rows;
}

/// Turn the rows that a Knack merge added into merged items with their prefab paths.
template <typename Paths>
[[nodiscard]] TypedMerge Finish(KnackMerge merge, const std::vector<std::vector<size_t>>& itemIndices, Paths&& paths)
{
	TypedMerge result;
	result.text = std::move(merge.text);
	for (auto& [pack, message] : merge.errors)
	{
		result.errors.push_back({.pack = std::move(pack), .message = std::move(message)});
	}
	for (auto& added : merge.added)
	{
		auto [prefabPath, templatePath] = paths(added);
		std::vector<std::pair<std::string, std::string>> row;
		for (const auto& [key, json] : added.fields)
		{
			row.emplace_back(key, json);
		}
		result.merged.push_back(MergedResource {
		    .pack = added.pack,
		    .resource = itemIndices.at(added.pack).at(added.row),
		    .name = added.name,
		    .id = KnackFieldText(added, "id"),
		    .prefabPath = std::move(prefabPath),
		    .templatePrefabPath = std::move(templatePath),
		    .row = std::move(row),
		});
	}
	return result;
}

[[nodiscard]] std::string ItemId(const Pack& pack, const PackResource& item)
{
	return item.id.value_or(DefaultResourceId(pack.id, item.name));
}

/// Read a position of the research tree, "(x, y, z)".
[[nodiscard]] std::optional<std::array<double, 3>> ParsePosition(std::string_view text)
{
	if (text.size() < 2 || text.front() != '(' || text.back() != ')')
	{
		return std::nullopt;
	}
	text = text.substr(1, text.size() - 2);
	std::array<double, 3> position {};
	for (size_t i = 0; i < 3; ++i)
	{
		const auto comma = text.find(',');
		auto part = text.substr(0, comma);
		while (!part.empty() && part.front() == ' ')
		{
			part.remove_prefix(1);
		}
		const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), position[i]);
		if (error != std::errc {} || end != part.data() + part.size())
		{
			return std::nullopt;
		}
		if (comma == std::string_view::npos)
		{
			return i == 2 ? std::optional(position) : std::nullopt;
		}
		text.remove_prefix(comma + 1);
	}
	return std::nullopt;
}

/// Replace the level number at the end of a research key or name, for example "HandPowerFireballL5" to
/// "HandPowerFireballL6". Return nothing if the text does not end with "L" and the old level.
[[nodiscard]] std::optional<std::string> WithLevel(std::string_view text, int oldLevel, int newLevel)
{
	const auto suffix = std::format("L{}", oldLevel);
	if (!text.ends_with(suffix))
	{
		return std::nullopt;
	}
	return std::format("{}L{}", text.substr(0, text.size() - suffix.size()), newLevel);
}

/// The state of one power during the spell merge.
struct PowerState
{
	int gameLevel = 0;                     ///< The highest level of the game.
	int topLevel = 0;                      ///< The highest level so far, with the added levels.
	const OrderedJson* research = nullptr; ///< The research row that unlocks `gameLevel`.
	const OrderedJson* before = nullptr;   ///< The research row that unlocks `gameLevel - 1`.
	std::string topKey;                    ///< The research key of `topLevel`.
};

} // namespace

moa::Result<TypedMerge> MergeThrowableRows(std::string_view tableText, std::span<const Pack> packs)
{
	auto table = ParseTable(tableText);
	if (!table)
	{
		return table.GetError();
	}
	std::vector<std::vector<size_t>> indices;
	const auto rows = CollectRows(
	    packs, PackKind::Throwable,
	    [](const Pack& pack, const PackResource& item) {
		    KnackRowRequest request {
		        .name = item.name, .templateName = item.templateName, .values = {}, .uniqueFields = {"id", "m_prefabName"}};
		    request.values = {{"id", ItemId(pack, item)},
		                      {"m_prefabName", item.prefab},
		                      {"m_location", std::string(k_PackDecorationFolder)},
		                      {"m_type", std::string("Throwable")}};
		    AddTypedValues(item, request);
		    return request;
	    },
	    indices);
	auto merge = MergeKnackRows(tableText, rows, {.keyField = "m_name", .keyIgnoresCase = true, .levelField = {}});
	if (!merge)
	{
		return merge.GetError();
	}
	return Finish(std::move(*merge), indices, [&](const KnackAddedRow& added) {
		const auto& item = packs[added.pack].resources[indices[added.pack][added.row]];
		const auto* templateRow = FindRow(*table, "m_name", item.templateName);
		auto templatePath = templateRow == nullptr
		                        ? std::string {}
		                        : std::format("{}/{}", Text(*templateRow, "m_location"), Text(*templateRow, "m_prefabName"));
		return std::pair {std::format("{}/{}", KnackFieldText(added, "m_location"), KnackFieldText(added, "m_prefabName")),
		                  std::move(templatePath)};
	});
}

moa::Result<TypedMerge> MergeWeaponRows(std::string_view tableText, std::span<const Pack> packs)
{
	auto table = ParseTable(tableText);
	if (!table)
	{
		return table.GetError();
	}
	// A weapon part must copy a block of the weapons. The merge refuses the whole pack if one part does not.
	std::vector<PackError> lineErrors;
	std::vector<Pack> checked;
	for (const auto& pack : packs)
	{
		std::optional<std::string> error;
		for (const auto& item : pack.resources)
		{
			if (item.kind != PackKind::Weapon)
			{
				continue;
			}
			const auto* templateRow = FindRow(*table, "m_prefabName", item.templateName);
			if (templateRow != nullptr && Text(*templateRow, "m_nGProductInfo") != k_WeaponProductLine)
			{
				error = std::format("{}: the template \"{}\" is not a block of the product line {}", item.name,
				                    item.templateName, k_WeaponProductLine);
				break;
			}
		}
		checked.push_back(pack);
		if (error)
		{
			lineErrors.push_back({.pack = pack.id, .message = std::move(*error)});
			std::erase_if(checked.back().resources, [](const PackResource& item) { return item.kind == PackKind::Weapon; });
		}
	}

	std::vector<std::vector<size_t>> indices;
	const auto rows = CollectRows(
	    std::span<const Pack>(checked), PackKind::Weapon,
	    [](const Pack& pack, const PackResource& item) {
		    KnackRowRequest request {
		        .name = item.name, .templateName = item.templateName, .values = {}, .uniqueFields = {"id", "m_prefabPath"}};
		    request.values = {{"id", ItemId(pack, item)},
		                      {"m_prefabPath", std::format("{}/{}/{}", k_PackBlockFolder, pack.id, item.name)}};
		    if (const auto title = PackValue(item, "title"))
		    {
			    request.values.emplace_back("m_tooltip", *title);
		    }
		    AddTypedValues(item, request);
		    return request;
	    },
	    indices);
	auto merge = MergeKnackRows(tableText, rows, {.keyField = "m_prefabName", .keyIgnoresCase = true, .levelField = {}});
	if (!merge)
	{
		return merge.GetError();
	}
	// The indices of the items refer to the packs before the check; the check removed only whole kinds, so the order
	// of the other items stays. Find each item again by its name.
	auto result = Finish(std::move(*merge), indices, [&](const KnackAddedRow& added) {
		const auto& item = checked[added.pack].resources[indices[added.pack][added.row]];
		const auto* templateRow = FindRow(*table, "m_prefabName", item.templateName);
		auto templatePath =
		    templateRow == nullptr ? std::string {} : std::format("{}{}", k_BlocksFolder, Text(*templateRow, "m_prefabPath"));
		return std::pair {std::format("{}{}", k_BlocksFolder, KnackFieldText(added, "m_prefabPath")), std::move(templatePath)};
	});
	for (auto& merged : result.merged)
	{
		const auto& original = packs[merged.pack].resources;
		const auto found = std::ranges::find_if(
		    original, [&](const PackResource& item) { return item.kind == PackKind::Weapon && item.name == merged.name; });
		merged.resource = static_cast<size_t>(found - original.begin());
	}
	result.errors.insert(result.errors.begin(), lineErrors.begin(), lineErrors.end());
	return result;
}

moa::Result<SpellMerge> MergeSpellRows(std::string_view handPowerTable, std::string_view researchTable,
                                       std::span<const Pack> packs)
{
	auto powers = ParseTable(handPowerTable);
	if (!powers)
	{
		return powers.GetError();
	}
	auto research = ParseTable(researchTable);
	if (!research)
	{
		return research.GetError();
	}

	// The highest level of each power, and the research rows of its two highest levels.
	std::map<std::string, PowerState> states;
	for (const auto power : k_SpellPowers)
	{
		PowerState state;
		for (const auto& row : *powers)
		{
			if (EqualsIgnoreCase(Text(row, "m_name"), power))
			{
				state.gameLevel = std::max(state.gameLevel, std::atoi(Text(row, "m_level").c_str()));
			}
		}
		state.topLevel = state.gameLevel;
		const auto unlockOf = [&](int level) { return std::format("m_handPower{}Level={}", power, level); };
		for (const auto& row : *research)
		{
			const auto unlock = Text(row, "m_unlock");
			if (EqualsIgnoreCase(unlock, unlockOf(state.gameLevel)))
			{
				state.research = &row;
			}
			else if (EqualsIgnoreCase(unlock, unlockOf(state.gameLevel - 1)))
			{
				state.before = &row;
			}
		}
		if (state.research != nullptr)
		{
			state.topKey = Text(*state.research, "m_key");
		}
		states.emplace(std::string(power), state);
	}

	SpellMerge result;
	std::vector<KnackPackRows> powerRows;
	std::vector<KnackPackRows> researchRows;
	std::vector<KnackFieldPatch> patches;
	for (size_t packIndex = 0; packIndex < packs.size(); ++packIndex)
	{
		const auto& pack = packs[packIndex];
		std::vector<size_t> spellIndices;
		for (size_t i = 0; i < pack.resources.size(); ++i)
		{
			if (pack.resources[i].kind == PackKind::Spell)
			{
				spellIndices.push_back(i);
			}
		}
		if (spellIndices.empty())
		{
			continue;
		}
		const auto levelOf = [&](size_t index) {
			const auto level = PackValue(pack.resources[index], "level");
			return level ? static_cast<int>(std::get<double>(*level)) : 0;
		};
		std::ranges::stable_sort(spellIndices, {}, levelOf);

		auto trial = states;
		KnackPackRows packPowers {.pack = pack.id, .rows = {}};
		KnackPackRows packResearch {.pack = pack.id, .rows = {}};
		std::vector<KnackFieldPatch> packPatches;
		std::vector<SpellLevel> packAdded;
		std::vector<std::pair<std::string, std::string>> packLinks; ///< A research key of an earlier pack, and its link.
		std::optional<std::string> error;
		for (const auto index : spellIndices)
		{
			const auto& spell = pack.resources[index];
			auto& state = trial.at(spell.templateName);
			const auto level = levelOf(index);
			if (state.gameLevel < 2 || state.research == nullptr)
			{
				error = std::format("{}: the game has no research row for the level {} of {}", spell.name, state.gameLevel,
				                    spell.templateName);
				break;
			}
			if (level != state.topLevel + 1)
			{
				error = std::format("{}: the next level of {} is {}", spell.name, spell.templateName, state.topLevel + 1);
				break;
			}
			const auto templateLevelValue = PackValue(spell, "templateLevel");
			const int templateLevel =
			    templateLevelValue ? static_cast<int>(std::get<double>(*templateLevelValue)) : state.gameLevel;
			if (templateLevel > state.gameLevel)
			{
				error = std::format("{}: the game has no level {} of {}", spell.name, templateLevel, spell.templateName);
				break;
			}

			// The hand power row: a copy of the row of the template level.
			KnackRowRequest powerRow {
			    .name = spell.templateName,
			    .templateName = spell.templateName,
			    .templateLevel = std::to_string(templateLevel),
			    .values = {{"id", ItemId(pack, spell)}, {"m_indexer", spell.name}, {"m_level", static_cast<double>(level)}},
			    .uniqueFields = {"id"}};
			AddTypedValues(spell, powerRow);
			// The research fields of the spell go into the research row, not into the hand power row.
			std::erase_if(powerRow.values, [](const auto& value) {
				return value.first == "m_title" || value.first == "m_description" || value.first == "m_icon" ||
				       value.first == "m_dollarCost" || value.first == "m_factionCost";
			});
			for (const auto key : {"title", "description"})
			{
				if (const auto value = PackValue(spell, key))
				{
					powerRow.values.emplace_back(key == std::string_view("title") ? "m_title" : "m_description", *value);
				}
			}
			packPowers.rows.push_back(std::move(powerRow));

			// The research row: a copy of the row that unlocks the highest level of the game.
			const auto& templateResearch = *state.research;
			const auto templateKey = Text(templateResearch, "m_key");
			auto key = WithLevel(templateKey, state.gameLevel, level);
			auto name = WithLevel(Text(templateResearch, "m_name"), state.gameLevel, level);
			if (!key || !name)
			{
				error = std::format("{}: the research key {} does not end with the level", spell.name, templateKey);
				break;
			}
			KnackRowRequest researchRow {
			    .name = *key, .templateName = templateKey, .values = {}, .uniqueFields = {"id", "m_name"}};
			researchRow.values = {
			    {"id", DefaultResourceId(pack.id, spell.name + "/research")},
			    {"m_name", *name},
			    {"m_linkToString", std::string {}},
			    {"m_unlock", std::format("m_handPower{}Level={}", spell.templateName, level)},
			    {"m_activatedAtStart", false},
			};
			const auto title =
			    PackValue(spell, "researchTitle").value_or(KnackValue(std::format("{} Lvl {}", spell.templateName, level)));
			researchRow.values.emplace_back("m_title", title);
			researchRow.values.emplace_back("m_newDisplayName", title);
			if (const auto description = PackValue(spell, "researchDescription"))
			{
				researchRow.values.emplace_back("m_description", *description);
				researchRow.values.emplace_back("m_newDescription", *description);
			}
			for (const auto [packKey, field] : {std::pair {"dollarCost", "m_dollarCost"},
			                                    std::pair {"factionCost", "m_factionCost"}, std::pair {"icon", "m_icon"}})
			{
				if (const auto value = PackValue(spell, packKey))
				{
					researchRow.values.emplace_back(field, *value);
				}
			}
			// The details name the hand power row of the level, for example "MAHandPowerInfo[Fireball5].m_baseDamage".
			auto details = Text(templateResearch, "m_newDetailsLine");
			const auto oldIndexer = std::format("[{}{}]", spell.templateName, state.gameLevel);
			const auto newIndexer = std::format("[{}]", spell.name);
			for (auto at = details.find(oldIndexer); at != std::string::npos;
			     at = details.find(oldIndexer, at + newIndexer.size()))
			{
				details.replace(at, oldIndexer.size(), newIndexer);
			}
			researchRow.values.emplace_back("m_newDetailsLine", details);
			// The place in the tree: one step further, in the direction of the last two levels of the game.
			const auto top = ParsePosition(Text(templateResearch, "m_position"));
			const auto before = state.before != nullptr ? ParsePosition(Text(*state.before, "m_position")) : std::nullopt;
			if (top)
			{
				std::array<double, 3> step {-200.0, 0.0, 0.0};
				if (before)
				{
					step = {(*top)[0] - (*before)[0], (*top)[1] - (*before)[1], 0.0};
				}
				const double steps = level - state.gameLevel;
				researchRow.values.emplace_back("m_position",
				                                std::format("({:.2f}, {:.2f}, {:.2f})", (*top)[0] + step[0] * steps,
				                                            (*top)[1] + step[1] * steps, (*top)[2]));
			}

			// The link from the research row before it: a game row gets a patch, a row of this merge gets the value.
			const auto link = [&](std::string_view existing) {
				return existing.empty() ? *key : std::format("{}|{}", existing, *key);
			};
			if (state.topLevel == state.gameLevel)
			{
				const auto current = Text(*FindRow(*research, "m_key", state.topKey), "m_linkToString");
				const auto patch = std::ranges::find(patches, state.topKey, &KnackFieldPatch::key);
				const auto existing =
				    patch != patches.end() ? OrderedJson::parse(patch->jsonValue, nullptr, false).get<std::string>() : current;
				packPatches.push_back(
				    {.key = state.topKey, .field = "m_linkToString", .jsonValue = OrderedJson(link(existing)).dump()});
			}
			else if (const auto found = std::ranges::find_if(
			             packResearch.rows, [&](const KnackRowRequest& row) { return row.name == state.topKey; });
			         found != packResearch.rows.end())
			{
				std::get<std::string>(
				    std::ranges::find(found->values, "m_linkToString", &std::pair<std::string, KnackValue>::first)->second) =
				    *key;
			}
			else
			{
				// The level before it comes from a pack before this one. Its row gets the link if this pack merges.
				packLinks.emplace_back(state.topKey, *key);
			}
			packResearch.rows.push_back(std::move(researchRow));
			packAdded.push_back({.pack = packIndex,
			                     .resource = index,
			                     .power = spell.templateName,
			                     .level = level,
			                     .researchKey = *key,
			                     .unlock = std::format("m_handPower{}Level={}", spell.templateName, level)});
			state.topLevel = level;
			state.topKey = *key;
		}

		// The hand power rows must merge: the trial merge with the packs so far and this pack.
		if (!error)
		{
			auto trialRows = powerRows;
			trialRows.push_back(packPowers);
			auto check = MergeKnackRows(handPowerTable, trialRows,
			                            {.keyField = "m_name", .keyIgnoresCase = true, .levelField = "m_level"});
			if (!check)
			{
				return check.GetError();
			}
			if (!check->errors.empty())
			{
				error = check->errors.back().second;
			}
		}
		if (error)
		{
			result.errors.push_back({.pack = pack.id, .message = std::move(*error)});
			continue;
		}
		states = std::move(trial);
		for (const auto& [from, to] : packLinks)
		{
			for (auto& rows : researchRows)
			{
				for (auto& row : rows.rows)
				{
					if (row.name == from)
					{
						std::get<std::string>(
						    std::ranges::find(row.values, "m_linkToString", &std::pair<std::string, KnackValue>::first)
						        ->second) = to;
					}
				}
			}
		}
		powerRows.push_back(std::move(packPowers));
		researchRows.push_back(std::move(packResearch));
		for (auto& patch : packPatches)
		{
			if (auto existing = std::ranges::find(patches, patch.key, &KnackFieldPatch::key); existing != patches.end())
			{
				*existing = std::move(patch);
			}
			else
			{
				patches.push_back(std::move(patch));
			}
		}
		result.added.insert(result.added.end(), packAdded.begin(), packAdded.end());
	}

	auto handPowers =
	    MergeKnackRows(handPowerTable, powerRows, {.keyField = "m_name", .keyIgnoresCase = true, .levelField = "m_level"});
	if (!handPowers)
	{
		return handPowers.GetError();
	}
	result.handPowers = std::move(handPowers->text);
	auto patched = PatchKnackFields(researchTable, patches, "m_key");
	if (!patched)
	{
		return patched.GetError();
	}
	auto researchMerge =
	    MergeKnackRows(*patched, researchRows, {.keyField = "m_key", .keyIgnoresCase = false, .levelField = {}});
	if (!researchMerge)
	{
		return researchMerge.GetError();
	}
	for (auto& [pack, message] : researchMerge->errors)
	{
		result.errors.push_back({.pack = pack, .message = std::format("research: {}", message)});
	}
	result.research = std::move(researchMerge->text);
	return result;
}

} // namespace moa_mod::objects
