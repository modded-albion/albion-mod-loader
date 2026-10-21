/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "moa_mod/objects/CarriableMerge.h"

#include <algorithm>
#include <format>
#include <optional>

#include <nlohmann/json.hpp>

namespace moa_mod::objects
{
namespace
{

using OrderedJson = nlohmann::ordered_json;
using Row = std::vector<std::pair<std::string, std::string>>;

constexpr std::string_view k_ByteOrderMark = "\xEF\xBB\xBF";

/// The fields of a game row that the merge compares.
struct GameRow
{
	std::string name;
	std::string id;
	std::string prefab;
	Row fields;
};

[[nodiscard]] std::string ValueText(const OrderedJson& value)
{
	return value.is_string() ? value.get<std::string>() : value.dump();
}

[[nodiscard]] std::string FieldText(const Row& row, std::string_view key)
{
	const auto found = std::ranges::find(row, key, &std::pair<std::string, std::string>::first);
	return found == row.end() ? std::string {} : found->second;
}

void SetField(Row& row, std::string_view key, std::string value)
{
	const auto found = std::ranges::find(row, key, &std::pair<std::string, std::string>::first);
	if (found == row.end())
	{
		row.emplace_back(std::string(key), std::move(value));
		return;
	}
	found->second = std::move(value);
}

[[nodiscard]] bool HasPrefab(std::string_view prefab)
{
	return !prefab.empty() && prefab != "?";
}

[[nodiscard]] std::string SerialiseRow(const Row& row)
{
	OrderedJson object = OrderedJson::object();
	for (const auto& [key, value] : row)
	{
		object[key] = value;
	}
	return object.dump(-1, ' ', false, OrderedJson::error_handler_t::replace);
}

/// The rows that the merge added. The merge checks a new resource against these rows and the game rows.
struct AddedRow
{
	std::string pack;
	std::string name;
	std::string id;
	std::string prefab;
};

/// Check one resource and make its row. Return an error text if the pack cannot use the resource.
[[nodiscard]] moa::Result<MergedResource> MakeRow(const Pack& pack, const PackResource& resource,
                                                  const std::vector<GameRow>& gameRows, const std::vector<AddedRow>& added)
{
	const auto id = resource.id.value_or(DefaultResourceId(pack.id, resource.name));
	for (const auto& game : gameRows)
	{
		if (EqualsIgnoreCase(game.name, resource.name))
		{
			return moa::MakeError("{}: the game has a row with the name \"{}\"", resource.name, game.name);
		}
		if (EqualsIgnoreCase(game.id, id))
		{
			return moa::MakeError("{}: the game row \"{}\" has the id \"{}\"", resource.name, game.name, id);
		}
		if (HasPrefab(game.prefab) && EqualsIgnoreCase(game.prefab, resource.prefab))
		{
			return moa::MakeError("{}: the game row \"{}\" has the prefab \"{}\"", resource.name, game.name, game.prefab);
		}
	}
	for (const auto& other : added)
	{
		if (EqualsIgnoreCase(other.name, resource.name))
		{
			return moa::MakeError("{}: the pack \"{}\" has a resource with the name \"{}\"", resource.name, other.pack,
			                      other.name);
		}
		if (EqualsIgnoreCase(other.id, id))
		{
			return moa::MakeError("{}: the resource \"{}\" of the pack \"{}\" has the id \"{}\"", resource.name, other.name,
			                      other.pack, id);
		}
		if (EqualsIgnoreCase(other.prefab, resource.prefab))
		{
			return moa::MakeError("{}: the resource \"{}\" of the pack \"{}\" has the prefab \"{}\"", resource.name, other.name,
			                      other.pack, other.prefab);
		}
	}

	const auto templateRow = std::ranges::find(gameRows, resource.templateName, &GameRow::name);
	if (templateRow == gameRows.end())
	{
		return moa::MakeError("{}: the template \"{}\" is not a row of the game", resource.name, resource.templateName);
	}
	if (!HasPrefab(templateRow->prefab))
	{
		return moa::MakeError("{}: the template \"{}\" has no prefab", resource.name, resource.templateName);
	}

	Row row = templateRow->fields;
	for (const auto field : k_CarriableFields)
	{
		if (std::ranges::find(row, field, &std::pair<std::string, std::string>::first) == row.end())
		{
			row.emplace_back(std::string(field), std::string {});
		}
	}
	SetField(row, "id", id);
	SetField(row, "m_name", resource.name);
	SetField(row, "m_prefab", resource.prefab);
	for (const auto& [key, value] : resource.row)
	{
		SetField(row, key, value);
	}

	return MergedResource {
	    .pack = 0,
	    .resource = 0,
	    .name = resource.name,
	    .id = id,
	    .prefabPath = std::format("{}{}", k_RawMaterialsFolder, resource.prefab),
	    .templatePrefabPath = std::format("{}{}", k_RawMaterialsFolder, templateRow->prefab),
	    .row = std::move(row),
	};
}

} // namespace

moa::Result<CarriableMerge> MergeCarriableRows(std::string_view tableText, std::span<const Pack> packs)
{
	auto body = tableText;
	if (body.starts_with(k_ByteOrderMark))
	{
		body.remove_prefix(k_ByteOrderMark.size());
	}
	const auto table = OrderedJson::parse(body, nullptr, false);
	if (table.is_discarded() || !table.is_array())
	{
		return moa::MakeError("the table is not a JSON array");
	}
	const auto end = tableText.find_last_not_of(" \t\r\n");
	if (end == std::string_view::npos || tableText[end] != ']')
	{
		return moa::MakeError("the table does not end with \"]\"");
	}

	std::vector<GameRow> gameRows;
	gameRows.reserve(table.size());
	for (size_t i = 0; i < table.size(); ++i)
	{
		const auto& item = table[i];
		if (!item.is_object())
		{
			return moa::MakeError("the item {} of the table is not an object", i);
		}
		GameRow row;
		for (const auto& [key, value] : item.items())
		{
			row.fields.emplace_back(key, ValueText(value));
		}
		row.name = FieldText(row.fields, "m_name");
		row.id = FieldText(row.fields, "id");
		row.prefab = FieldText(row.fields, "m_prefab");
		gameRows.push_back(std::move(row));
	}

	CarriableMerge merge;
	std::vector<AddedRow> added;
	std::string inserted;
	bool empty = table.empty();
	for (size_t packIndex = 0; packIndex < packs.size(); ++packIndex)
	{
		const auto& pack = packs[packIndex];
		std::vector<MergedResource> packRows;
		std::vector<AddedRow> packAdded = added;
		std::optional<moa::Error> error;
		for (size_t resourceIndex = 0; resourceIndex < pack.resources.size(); ++resourceIndex)
		{
			const auto& resource = pack.resources[resourceIndex];
			if (resource.kind != PackKind::Resource)
			{
				continue;
			}
			auto row = MakeRow(pack, resource, gameRows, packAdded);
			if (!row)
			{
				error = row.GetError();
				break;
			}
			row->pack = packIndex;
			row->resource = resourceIndex;
			packAdded.push_back({.pack = pack.id, .name = row->name, .id = row->id, .prefab = resource.prefab});
			packRows.push_back(std::move(*row));
		}
		if (error)
		{
			merge.errors.push_back({.pack = pack.id, .message = std::move(error->message)});
			continue;
		}
		added = std::move(packAdded);
		for (auto& row : packRows)
		{
			if (!empty)
			{
				inserted += ',';
			}
			empty = false;
			inserted += SerialiseRow(row.row);
			merge.merged.push_back(std::move(row));
		}
	}

	merge.text.reserve(tableText.size() + inserted.size());
	merge.text.append(tableText.substr(0, end));
	merge.text.append(inserted);
	merge.text.append(tableText.substr(end));
	return merge;
}

const MergedResource* MatchResourcePath(std::string_view requestedPath, std::span<const MergedResource> merged)
{
	const auto same = [](char a, char b) {
		const auto normalise = [](char c) {
			if (c == '\\')
			{
				return '/';
			}
			return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
		};
		return normalise(a) == normalise(b);
	};
	const auto found = std::ranges::find_if(
	    merged, [&](const MergedResource& resource) { return std::ranges::equal(requestedPath, resource.prefabPath, same); });
	return found == merged.end() ? nullptr : &*found;
}

} // namespace moa_mod::objects
