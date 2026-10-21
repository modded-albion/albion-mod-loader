/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <array>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <common/Result.h>
#include <mod_core/Hotkey.h>
#include <mod_core/SettingsFields.h>
#include <nlohmann/json.hpp>

namespace moa_mod::loader
{

/// The settings of the loader in mods.json. The objects of the mods ("camera", "moon", "sky") belong to the mods: the
/// loader gives each mod its objects and does not read them. A missing field keeps its default.
struct LoaderSettings
{
	/// Write a heartbeat line to the log one time in each 3600 frames. The change works after a restart of the game.
	bool frameHook = false;

	/// Watch the settings file and use a changed file while the game runs, and read the hotkeys. The change works after
	/// a restart of the game.
	bool liveReload = true;

	/// The keys that change the mods while the game runs. They work only with `liveReload`.
	struct Hotkeys
	{
		bool enabled = true;         ///< Read the hotkeys in each frame.
		bool messages = true;        ///< Show a short message of the game when a hotkey changes something.
		double messageSeconds = 2.0; ///< The time that the message stays on the screen.
		/// The hotkeys that the file gives, by the name of the action, for example "toggleCamera". An action that is not
		/// in the list uses its default hotkey.
		std::map<std::string, Hotkey> keys;

		friend bool operator==(const Hotkeys&, const Hotkeys&) = default;
	} hotkeys;

	/// The menu bar of the mods in the game. It shows while the menu of the game is open (the Escape key).
	struct Overlay
	{
		bool enabled = true;    ///< Install the menu bar. The change works after a restart of the game.
		double fontScale = 1.0; ///< The size of the text of the menu bar, as a multiple of the default size.

		friend bool operator==(const Overlay&, const Overlay&) = default;
	} overlay;

	/// Custom objects: the loader serves table rows and objects that the game loads by resource path. Each value works
	/// only after a restart of the game.
	struct Objects
	{
		bool enabled = false;    ///< Install the hook on the resource loads of the game.
		bool trace = true;       ///< Write each resource path and type that the game loads one time to a file.
		std::string traceFolder; ///< The folder of the trace file. Empty is the folder of the logs.
		/// The folder of the packs. Empty is the folder `packs` next to the settings file in the local application data.
		std::string packsFolder;
		/// Add one test resource: a copy of the raw cotton with the name `Custom_TestCotton`. For research only.
		bool testClone = false;

		friend bool operator==(const Objects&, const Objects&) = default;
	} objects;

	/// The map override: edited map data from the editor that the loader gives to the game in place of the data of the
	/// game. No file of the game changes. The value works only after a restart of the game.
	struct Maps
	{
		/// The name of the override: a folder in `%LOCALAPPDATA%/MastersOfAlbionReforged/maps`. Empty is no override.
		std::string active;

		friend bool operator==(const Maps&, const Maps&) = default;
	} maps;

	/// The mod zips in the folder `mods` of the game. Each zip loads, except the zips that the user turned off. The
	/// values work only after a restart of the game.
	struct Mods
	{
		/// The ids of the mods that the user turned off.
		std::vector<std::string> disabled;
		/// The mod whose map the game loads. No value: the first mod with a map, in the order of the ids. Empty: no map of
		/// a mod. A folder in `maps.active` comes before this value.
		std::optional<std::string> map;

		friend bool operator==(const Mods&, const Mods&) = default;
	} mods;

	/// The mod DLLs in the folder `mods` of the game. Each DLL loads, except the DLLs that the user or the start-up
	/// guard turned off. The values work only after a restart of the game.
	struct ModDlls
	{
		/// The file names of the DLLs that do not load, for example "better_skybox.dll".
		std::vector<std::string> disabled;

		friend bool operator==(const ModDlls&, const ModDlls&) = default;
	} modDlls;

	friend bool operator==(const LoaderSettings&, const LoaderSettings&) = default;
};

/// The top-level keys of mods.json that belong to the loader.
inline constexpr std::array<std::string_view, 8> k_LoaderKeys {"frameHook", "liveReload", "hotkeys", "overlay",
                                                               "objects",   "maps",       "mods",    "modDlls"};

/// Read the settings of the loader from the JSON text of mods.json. The objects of the mods stay unread. Return an
/// error if the text is not a JSON object, a field has a wrong type, or a value is out of range.
[[nodiscard]] moa::Result<LoaderSettings> ParseLoaderSettings(std::string_view json);

/// The settings of the loader as JSON, in the form that `ParseLoaderSettings` reads.
[[nodiscard]] nlohmann::json LoaderSettingsJson(const LoaderSettings& settings);

/// What new settings change in the running loader.
struct LoaderSettingsChange
{
	bool hotkeys = false; ///< A value of `hotkeys` changed.
	bool overlay = false; ///< A value of `overlay` changed, other than `enabled`.
	/// The names of the changed fields that the loader uses only after a restart of the game.
	std::vector<std::string> restartOnly;

	[[nodiscard]] bool Any() const { return hotkeys || overlay; }
	[[nodiscard]] nlohmann::json Describe() const;
};

/// Compare the settings that the loader uses now with new settings.
[[nodiscard]] LoaderSettingsChange CompareLoaderSettings(const LoaderSettings& current, const LoaderSettings& next);

/// The settings that the loader can use now: `next`, with the values of `current` for the fields that need a restart.
[[nodiscard]] LoaderSettings LiveLoaderSettings(const LoaderSettings& current, const LoaderSettings& next);

/// The fields of the loader in the menu bar.
[[nodiscard]] std::span<const settings::Field<LoaderSettings>> LoaderFields();

/// Change one field of the loader. Return the change of the settings file and a message.
[[nodiscard]] moa::Result<settings::SettingsEdit>
EditLoaderField(LoaderSettings& current, const settings::Field<LoaderSettings>& field, const settings::FieldValue& value);

} // namespace moa_mod::loader
