/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <common/Result.h>
#include <core/LoaderSettings.h>
#include <mod_core/SettingsFields.h>
#include <modzip/ModInfo.h>
#include <modzip/Zip.h>
#include <nlohmann/json.hpp>

namespace moa_mod::mods
{

/// Read a file by its path in a tree of files (a folder, or a folder in a zip), with "/" between the parts.
using FileReader = std::function<moa::Result<std::vector<std::byte>>(const std::string& relative)>;

/// What the mod does with a mod zip.
enum class ModState : uint8_t
{
	On,      ///< The mod loads.
	Off,     ///< The user turned the mod off in the Mods list.
	Refused, ///< The zip is not correct, is for another game build, or has the id of another zip.
};

[[nodiscard]] std::string_view ModStateName(ModState state);

/// One zip in the folder of the mods, and what the mod does with it.
struct CatalogueMod
{
	std::string file;                                       ///< The name of the zip file.
	std::optional<moa::modzip::ModInfo> info;               ///< No value if `mod.json` cannot be read.
	std::shared_ptr<const moa::modzip::ZipArchive> archive; ///< Null if the zip is refused.
	ModState state = ModState::Refused;
	std::string error;      ///< Why the zip is refused.
	bool mapActive = false; ///< The map of this mod is the map that the game loads.
};

/// Where the map override comes from.
struct MapChoice
{
	enum class Kind : uint8_t
	{
		None,   ///< No map override.
		Folder, ///< A folder in the development folder of the maps (`maps.active`).
		Mod,    ///< The map of a mod zip.
		/// The first mod with a map, in the load order. Only a choice of the user has this kind: `BuildCatalogue` gives the
		/// mod that it found.
		Automatic,
	};
	Kind kind = Kind::None;
	std::string name; ///< The folder name, or the mod id.

	friend bool operator==(const MapChoice&, const MapChoice&) = default;
};

/// All the mod zips, in the load order (the order of the ids; refused zips with no id last, by file name), and the
/// map that the game loads.
struct ModCatalogue
{
	std::vector<CatalogueMod> mods;
	MapChoice map;
	std::string mapNote; ///< Why the map is not the map that the settings ask for, or empty.

	/// The mods that load, in the load order.
	[[nodiscard]] std::vector<const CatalogueMod*> Loaded() const;
	/// The mod with this id that loads, or null.
	[[nodiscard]] const CatalogueMod* FindLoaded(std::string_view id) const;
};

/// Open one zip and check it: the zip itself (`ZipArchive::Open`), `mod.json`, the contents and the game build. A
/// zip that fails a check gets the state `Refused` and the reason; the other zips do not change.
[[nodiscard]] CatalogueMod OpenModZip(std::string file, std::vector<std::byte> bytes, std::string_view buildGuid,
                                      const moa::modzip::ZipLimits& limits = {});

/// Put the zips in the load order, keep only the newest version of each id (two zips with the same id and the same
/// newest version are both refused), apply the switches of the user, and choose the map.
///
/// The map: a folder in `maps.active` comes first. Else `mods.map`: empty is no map, and the id of a mod that loads
/// and has a map is that map. With no `mods.map`, or with a mod that does not load or has no map, the first mod in the
/// load order that has a map gives the map.
[[nodiscard]] ModCatalogue BuildCatalogue(std::vector<CatalogueMod> mods, const loader::LoaderSettings& settings);

/// A reader of the files of a folder in a zip: `prefix` is the folder, for example "map/" or "packs/a/". The reader
/// refuses a path that `CheckEntryName` refuses, so it cannot read outside the folder.
[[nodiscard]] FileReader ZipFolderReader(std::shared_ptr<const moa::modzip::ZipArchive> archive, std::string prefix);

/// Turn a mod on or off in the settings. Return the change of the settings file, as for a field of the menu bar.
[[nodiscard]] moa::Result<moa_mod::settings::SettingsEdit> EditModEnabled(loader::LoaderSettings& settings, std::string_view id,
                                                                          bool enabled);

/// Choose the map override in the settings. A folder or `Automatic` removes `mods.map`; a mod or no map clears
/// `maps.active`.
[[nodiscard]] moa::Result<moa_mod::settings::SettingsEdit> EditMapChoice(loader::LoaderSettings& settings,
                                                                         const MapChoice& choice);

/// The catalogue as JSON, for the menu bar, the inspector and the log.
[[nodiscard]] nlohmann::json DescribeCatalogue(const ModCatalogue& catalogue);

} // namespace moa_mod::mods
