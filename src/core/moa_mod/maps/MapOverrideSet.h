/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <moa/common/Result.h>
#include <moa/mapoverride/MapOverride.h>
#include <nlohmann/json.hpp>

namespace moa_mod::maps
{

/// The largest file of an override that the mod reads.
constexpr size_t k_MaxOverrideFileBytes = 256ull * 1024 * 1024;

/// Read a file of the folder of an override by its path in the folder (with `/`).
using OverrideFileReader = std::function<moa::Result<std::vector<std::byte>>(const std::string& relative)>;

/// What the mod did with an item.
enum class ItemState : uint8_t
{
	Waiting,  ///< The game did not load the data of the item yet.
	Applied,  ///< The game got the data of the override.
	Refused,  ///< The original data of the game was not the data that the editor read, or the change failed.
	Disabled, ///< An item before this one was refused, so the mod gives the game no more data of the override.
};

[[nodiscard]] std::string_view ItemStateName(ItemState state);

/// One item of an override, with its file and the parsed data of the land items.
struct OverrideItem
{
	moa::mapoverride::ManifestItem manifest;
	std::vector<std::byte> bytes;
	std::vector<int16_t> heights;                                ///< Heights items only.
	std::vector<moa::mapoverride::HolderMove> holders;           ///< TreeHolder items only.
	std::vector<moa::mapoverride::TerrainTreeMove> terrainTrees; ///< Tree items only.
	/// The SHA-1 of the data of the game in the form of `CheckOriginal` after the mod changed it, or empty for an item
	/// that the mod does not change in place.
	std::string appliedSha1;
	ItemState state = ItemState::Waiting;
	std::string message; ///< Why the item was refused, or what the mod changed.
	uint64_t serves = 0; ///< The loads that got the data of the override.
};

/// Return true if an item of this kind is in the land group: the heights, the trees of the land data, the camera floor,
/// the coast map and the TreeHolder objects. The game gets all of them or none.
[[nodiscard]] bool IsLandKind(moa::mapoverride::ItemKind kind);

/// A map override that the mod read and checked, and the state of each item while the game runs. The mod reads all
/// files at the start. If the manifest is for another game build or a file is not the file of the manifest, the read
/// fails and the mod gives the game nothing. While the game loads its data, the mod compares each original item with
/// the SHA-1 of the manifest. A refusal stops only the group of the item: each TextAsset is a group, and the land
/// items are one group.
class MapOverrideSet
{
public:
	/// Read the manifest and every file, and check them. `buildGuid` is the build of the running game.
	[[nodiscard]] static moa::Result<MapOverrideSet> Load(std::string name, std::string_view buildGuid,
	                                                      const OverrideFileReader& read);

	[[nodiscard]] const std::string& Name() const { return _name; }
	[[nodiscard]] const moa::mapoverride::Manifest& Manifest() const { return _manifest; }
	[[nodiscard]] std::span<const OverrideItem> Items() const { return _items; }

	/// Return the index of the item that the game loads with this resource path, or no value.
	[[nodiscard]] std::optional<size_t> FindPath(std::string_view resourcePath) const;

	/// Return the index of the item of a land kind (heights, TreeHolders, trees), or no value.
	[[nodiscard]] std::optional<size_t> FindKind(moa::mapoverride::ItemKind kind) const;

	/// True after an item of the group of `index` was refused. Then the game gets no more data of that group.
	[[nodiscard]] bool GroupStopped(size_t index) const;

	/// True after the land group was refused.
	[[nodiscard]] bool LandStopped() const { return _landStopped; }

	/// What the mod does with an item at a load.
	enum class Decision : uint8_t
	{
		Apply,          ///< The game has the original data: give it the data of the override.
		AlreadyApplied, ///< The game has the data of the override already (for example at a second load).
		Refuse,         ///< Give the game its own data.
	};

	/// Compare the data that the game has now (in the form of `CheckOriginal`) with the item, and change nothing. On a
	/// refusal, `reason` gets the reason.
	[[nodiscard]] Decision Check(size_t index, std::span<const std::byte> current, std::string* reason = nullptr) const;

	/// Decide what to do with an item, as `Check` does. If the group of the item was refused before, or the data is not
	/// the original data that the editor read, the function refuses the item and stops its group.
	[[nodiscard]] Decision Decide(size_t index, std::span<const std::byte> current);

	/// Record that an item was refused, or that its change failed. This stops the group of the item.
	void Fail(size_t index, std::string message);

	/// Record that the game got the data of the item, with a short text for the status.
	void Served(size_t index, std::string message);

	/// The state of the override and its items, for the inspector and the log.
	[[nodiscard]] nlohmann::json Describe() const;

private:
	MapOverrideSet() = default;

	std::string _name;
	moa::mapoverride::Manifest _manifest;
	std::vector<OverrideItem> _items;
	std::map<std::string, size_t, std::less<>> _paths;
	std::vector<bool> _textStopped;
	bool _landStopped = false;
};

/// The folders in the folder of the overrides that have a manifest, sorted by name. The menu bar of the mods lists them.
[[nodiscard]] std::vector<std::string> ListMapOverrides(const std::filesystem::path& mapsFolder);

} // namespace moa_mod::maps
