/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "moa_mod/maps/MapOverrideSet.h"

#include <algorithm>
#include <format>
#include <utility>

#include <moa/mapoverride/Sha1.h>

namespace moa_mod::maps
{

namespace mo = moa::mapoverride;

namespace
{
[[nodiscard]] std::string_view AsText(std::span<const std::byte> bytes)
{
	return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
} // namespace

std::string_view ItemStateName(ItemState state)
{
	switch (state)
	{
	case ItemState::Waiting:
		return "waiting";
	case ItemState::Applied:
		return "applied";
	case ItemState::Refused:
		return "refused";
	case ItemState::Disabled:
		return "disabled";
	}
	return "unknown";
}

moa::Result<MapOverrideSet> MapOverrideSet::Load(std::string name, std::string_view buildGuid, const OverrideFileReader& read)
{
	if (!mo::IsValidMapName(name))
	{
		return moa::MakeError("'{}' is not a name of a map override", name);
	}
	auto manifestBytes = read(std::string(mo::k_ManifestFile));
	if (!manifestBytes)
	{
		return moa::MakeError("the override '{}' has no manifest: {}", name, manifestBytes.GetError().message);
	}
	auto manifest = mo::ParseManifest(AsText(*manifestBytes));
	if (!manifest)
	{
		return moa::MakeError("the manifest of '{}': {}", name, manifest.GetError().message);
	}
	if (auto build = mo::CheckBuild(*manifest, buildGuid); !build)
	{
		return build.GetError();
	}
	MapOverrideSet set;
	set._name = std::move(name);
	for (const auto& entry : manifest->items)
	{
		auto bytes = read(entry.file);
		if (!bytes)
		{
			return moa::MakeError("the file '{}' of the override: {}", entry.file, bytes.GetError().message);
		}
		if (bytes->size() > k_MaxOverrideFileBytes)
		{
			return moa::MakeError("the file '{}' has {} bytes; the mod reads not more than {}", entry.file, bytes->size(),
			                      k_MaxOverrideFileBytes);
		}
		if (auto check = mo::CheckFile(entry, *bytes); !check)
		{
			return check.GetError();
		}
		OverrideItem item {.manifest = entry, .bytes = std::move(*bytes)};
		switch (entry.kind)
		{
		case mo::ItemKind::Text:
			break;
		case mo::ItemKind::Heights:
		{
			auto heights = mo::ReadHeights(entry, item.bytes);
			if (!heights)
			{
				return heights.GetError();
			}
			item.heights = std::move(*heights);
			item.appliedSha1 = entry.sha1;
			break;
		}
		case mo::ItemKind::CameraFloor:
		case mo::ItemKind::CoastMap:
			item.appliedSha1 = entry.sha1;
			break;
		case mo::ItemKind::TreeHolders:
		{
			auto moves = mo::ParseHolderMoves(AsText(item.bytes));
			if (!moves)
			{
				return moves.GetError();
			}
			auto applied = *moves;
			for (auto& move : applied)
			{
				move.before = move.after;
			}
			item.appliedSha1 = mo::Sha1Hex(mo::HolderOriginalBytes(applied));
			item.holders = std::move(*moves);
			break;
		}
		case mo::ItemKind::TerrainTrees:
		{
			auto moves = mo::ParseTerrainTreeMoves(AsText(item.bytes));
			if (!moves)
			{
				return moves.GetError();
			}
			auto applied = *moves;
			for (auto& move : applied)
			{
				move.before = move.after;
			}
			item.appliedSha1 = mo::Sha1Hex(mo::TerrainTreeOriginalBytes(applied));
			item.terrainTrees = std::move(*moves);
			break;
		}
		}
		for (const auto& path : entry.paths)
		{
			if (!set._paths.emplace(path, set._items.size()).second)
			{
				return moa::MakeError("two items of the override have the resource path '{}'", path);
			}
		}
		set._items.push_back(std::move(item));
	}
	set._manifest = std::move(*manifest);
	set._textStopped.assign(set._items.size(), false);
	return set;
}

std::optional<size_t> MapOverrideSet::FindPath(std::string_view resourcePath) const
{
	const auto found = _paths.find(mo::NormalResourcePath(resourcePath));
	return found == _paths.end() ? std::nullopt : std::optional<size_t>(found->second);
}

std::optional<size_t> MapOverrideSet::FindKind(mo::ItemKind kind) const
{
	const auto found = std::ranges::find(_items, kind, [](const OverrideItem& item) { return item.manifest.kind; });
	return found == _items.end() ? std::nullopt : std::optional<size_t>(static_cast<size_t>(found - _items.begin()));
}

bool IsLandKind(mo::ItemKind kind)
{
	return kind != mo::ItemKind::Text;
}

bool MapOverrideSet::GroupStopped(size_t index) const
{
	return IsLandKind(_items.at(index).manifest.kind) ? _landStopped : _textStopped.at(index);
}

MapOverrideSet::Decision MapOverrideSet::Check(size_t index, std::span<const std::byte> current, std::string* reason) const
{
	const auto& item = _items.at(index);
	if (GroupStopped(index))
	{
		if (reason != nullptr)
		{
			*reason = "an item of its group was refused before";
		}
		return Decision::Refuse;
	}
	if (!item.appliedSha1.empty() && mo::Sha1Hex(current) == item.appliedSha1)
	{
		return Decision::AlreadyApplied;
	}
	if (auto check = mo::CheckOriginal(item.manifest, current); !check)
	{
		if (reason != nullptr)
		{
			*reason = check.GetError().message;
		}
		return Decision::Refuse;
	}
	return Decision::Apply;
}

MapOverrideSet::Decision MapOverrideSet::Decide(size_t index, std::span<const std::byte> current)
{
	std::string reason;
	const auto decision = Check(index, current, &reason);
	if (decision == Decision::Refuse && !GroupStopped(index))
	{
		Fail(index, std::move(reason));
	}
	return decision;
}

void MapOverrideSet::Fail(size_t index, std::string message)
{
	auto& item = _items.at(index);
	item.state = ItemState::Refused;
	item.message = std::move(message);
	const bool land = IsLandKind(item.manifest.kind);
	if (land)
	{
		_landStopped = true;
	}
	else
	{
		_textStopped.at(index) = true;
	}
	for (size_t other = 0; other < _items.size(); ++other)
	{
		auto& entry = _items[other];
		if (other != index && land && IsLandKind(entry.manifest.kind) && entry.state != ItemState::Refused)
		{
			entry.state = ItemState::Disabled;
		}
	}
}

void MapOverrideSet::Served(size_t index, std::string message)
{
	auto& item = _items.at(index);
	item.state = ItemState::Applied;
	item.message = std::move(message);
	++item.serves;
}

nlohmann::json MapOverrideSet::Describe() const
{
	nlohmann::json items = nlohmann::json::array();
	for (const auto& item : _items)
	{
		items.push_back({
		    {"kind", mo::ItemKindName(item.manifest.kind)},
		    {"name", item.manifest.name},
		    {"file", item.manifest.file},
		    {"paths", item.manifest.paths},
		    {"state", ItemStateName(item.state)},
		    {"serves", item.serves},
		    {"message", item.message},
		});
	}
	return {
	    {"name", _name},  {"buildGuid", _manifest.buildGuid}, {"made", _manifest.made}, {"landStopped", _landStopped},
	    {"items", items},
	};
}

std::vector<std::string> ListMapOverrides(const std::filesystem::path& mapsFolder)
{
	std::vector<std::string> names;
	std::error_code error;
	for (const auto& entry : std::filesystem::directory_iterator(mapsFolder, error))
	{
		const auto name = entry.path().filename().string();
		if (entry.is_directory(error) && mo::IsValidMapName(name) &&
		    std::filesystem::is_regular_file(entry.path() / std::string(mo::k_ManifestFile), error))
		{
			names.push_back(name);
		}
	}
	std::ranges::sort(names);
	return names;
}

} // namespace moa_mod::maps
