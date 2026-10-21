/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "moa_mod/objects/PackTables.h"

#include <algorithm>
#include <format>

namespace moa_mod::objects
{

std::optional<PackTable> PackTableOfPath(std::string_view normalPath)
{
	const auto found = std::ranges::find(k_PackTablePaths, normalPath, &std::pair<PackTable, std::string_view>::second);
	return found == k_PackTablePaths.end() ? std::nullopt : std::optional(found->first);
}

std::string_view PackTablePath(PackTable table)
{
	return std::ranges::find(k_PackTablePaths, table, &std::pair<PackTable, std::string_view>::first)->second;
}

bool PacksChangeTable(std::span<const Pack> packs, PackTable table)
{
	const auto kind = [table] {
		switch (table)
		{
		case PackTable::Decoration:
			return PackKind::Throwable;
		case PackTable::Block:
			return PackKind::Weapon;
		case PackTable::HandPower:
		case PackTable::Research:
			return PackKind::Spell;
		case PackTable::Carriable:
			break;
		}
		return PackKind::Resource;
	}();
	return std::ranges::any_of(packs, [kind](const Pack& pack) {
		return std::ranges::any_of(pack.resources, [kind](const PackResource& item) { return item.kind == kind; });
	});
}

PackTableMerger::PackTableMerger(std::vector<Pack> packs)
    : _packs(std::move(packs))
{
}

std::optional<std::string> PackTableMerger::Merge(PackTable table, std::string_view text, const TextLoader& loadOriginal)
{
	if (!PacksChangeTable(_packs, table))
	{
		return std::nullopt;
	}
	const auto fail = [&](const moa::Error& error) -> std::optional<std::string> {
		_errors.push_back({.pack = std::string(PackTablePath(table)), .message = error.message});
		return std::nullopt;
	};
	const auto keep = [&](std::vector<PackError> errors) {
		_errors.insert(_errors.end(), std::make_move_iterator(errors.begin()), std::make_move_iterator(errors.end()));
	};
	switch (table)
	{
	case PackTable::Carriable:
	{
		auto merge = MergeCarriableRows(text, _packs);
		if (!merge)
		{
			return fail(merge.GetError());
		}
		keep(std::move(merge->errors));
		_merged[table] = std::move(merge->merged);
		return std::move(merge->text);
	}
	case PackTable::Decoration:
	case PackTable::Block:
	{
		auto merge = table == PackTable::Decoration ? MergeThrowableRows(text, _packs) : MergeWeaponRows(text, _packs);
		if (!merge)
		{
			return fail(merge.GetError());
		}
		keep(std::move(merge->errors));
		_merged[table] = std::move(merge->merged);
		return std::move(merge->text);
	}
	case PackTable::HandPower:
	case PackTable::Research:
		break;
	}

	// The spells change two tables. The merge needs the texts of both; the other one comes from the last input or the
	// game.
	const auto other = table == PackTable::HandPower ? PackTable::Research : PackTable::HandPower;
	if (const auto found = _spellInputs.find(table); found != _spellInputs.end() && found->second == text)
	{
		return _spellOutputs.at(table);
	}
	_spellInputs[table] = std::string(text);
	if (!_spellInputs.contains(other))
	{
		auto original = loadOriginal ? loadOriginal(other) : std::nullopt;
		if (!original)
		{
			_spellInputs.erase(table);
			return fail(moa::MakeError("the table {} cannot be read", PackTablePath(other)));
		}
		_spellInputs[other] = std::move(*original);
	}
	auto merge = MergeSpellRows(_spellInputs.at(PackTable::HandPower), _spellInputs.at(PackTable::Research), _packs);
	if (!merge)
	{
		_spellInputs.erase(table);
		return fail(merge.GetError());
	}
	keep(std::move(merge->errors));
	_spells = std::move(merge->added);
	_spellOutputs[PackTable::HandPower] = std::move(merge->handPowers);
	_spellOutputs[PackTable::Research] = std::move(merge->research);
	return _spellOutputs.at(table);
}

std::vector<MergedResource> PackTableMerger::Merged() const
{
	std::vector<MergedResource> all;
	for (const auto& [table, items] : _merged)
	{
		all.insert(all.end(), items.begin(), items.end());
	}
	return all;
}

std::vector<MergedResource> PackTableMerger::MergedIn(PackTable table) const
{
	const auto found = _merged.find(table);
	return found != _merged.end() ? found->second : std::vector<MergedResource> {};
}

std::vector<PackError> PackTableMerger::TakeErrors()
{
	return std::exchange(_errors, {});
}

} // namespace moa_mod::objects
