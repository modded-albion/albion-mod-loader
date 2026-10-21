/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <array>
#include <optional>
#include <utility>

#include <core/LoaderSettings.h>
#include <mapoverride/MapOverride.h>
#include <modzip/ModInfo.h>

namespace moa_mod::loader
{

using settings::Field;
using settings::FieldKind;
using settings::ReadField;

namespace
{
/// Read the object `mods`. Return the error, if there is one.
std::optional<moa::Error> ReadMods(const nlohmann::json& object, LoaderSettings::Mods& value)
{
	if (!object.is_object())
	{
		return moa::MakeError("'mods' must be an object");
	}
	for (const auto& [key, field] : object.items())
	{
		if (key != "disabled" && key != "map")
		{
			return moa::MakeError("'mods.{}' is not a setting", key);
		}
	}
	if (const auto disabled = object.find("disabled"); disabled != object.end())
	{
		if (!disabled->is_array())
		{
			return moa::MakeError("'mods.disabled' must be a list of mod ids");
		}
		value.disabled.clear();
		for (const auto& id : *disabled)
		{
			if (!id.is_string() || !moa::modzip::IsValidModId(id.get<std::string>()))
			{
				return moa::MakeError("'mods.disabled' must be a list of mod ids");
			}
			value.disabled.push_back(id.get<std::string>());
		}
	}
	if (const auto map = object.find("map"); map != object.end())
	{
		if (!map->is_string() || (!map->get<std::string>().empty() && !moa::modzip::IsValidModId(map->get<std::string>())))
		{
			return moa::MakeError("'mods.map' must be empty or a mod id");
		}
		value.map = map->get<std::string>();
	}
	return std::nullopt;
}

/// Return true if the name is a plain file name of a DLL: no folder, and the extension ".dll".
[[nodiscard]] bool IsDllFileName(std::string_view name)
{
	if (name.size() < 5 || name.find_first_of("/\\:") != std::string_view::npos || name.starts_with('.'))
	{
		return false;
	}
	auto extension = std::string(name.substr(name.size() - 4));
	for (auto& c : extension)
	{
		c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
	}
	return extension == ".dll";
}

/// Read the object `modDlls`. Return the error, if there is one.
std::optional<moa::Error> ReadModDlls(const nlohmann::json& object, LoaderSettings::ModDlls& value)
{
	if (!object.is_object())
	{
		return moa::MakeError("'modDlls' must be an object");
	}
	if (const auto disabled = object.find("disabled"); disabled != object.end())
	{
		if (!disabled->is_array())
		{
			return moa::MakeError("'modDlls.disabled' must be a list of DLL file names");
		}
		value.disabled.clear();
		for (const auto& name : *disabled)
		{
			if (!name.is_string() || !IsDllFileName(name.get<std::string>()))
			{
				return moa::MakeError("'modDlls.disabled' must be a list of DLL file names");
			}
			value.disabled.push_back(name.get<std::string>());
		}
	}
	return std::nullopt;
}

/// Read the object `hotkeys`. Each field other than `enabled`, `messages` and `messageSeconds` is the hotkey of an
/// action. Return the error, if there is one.
std::optional<moa::Error> ReadHotkeys(const nlohmann::json& object, LoaderSettings::Hotkeys& value)
{
	if (!object.is_object() || !ReadField(object, "enabled", value.enabled) || !ReadField(object, "messages", value.messages) ||
	    !ReadField(object, "messageSeconds", value.messageSeconds))
	{
		return moa::MakeError("the object 'hotkeys' has a field with a wrong type");
	}
	if (value.messageSeconds < 0.5 || value.messageSeconds > 30.0)
	{
		return moa::MakeError("'messageSeconds' must be 0.5 to 30 seconds");
	}
	for (const auto& [name, field] : object.items())
	{
		if (name == "enabled" || name == "messages" || name == "messageSeconds")
		{
			continue;
		}
		if (!field.is_string())
		{
			return moa::MakeError("the hotkey '{}' must be a text such as \"Ctrl+Shift+F5\"", name);
		}
		auto hotkey = ParseHotkey(field.get<std::string>());
		if (!hotkey)
		{
			return moa::MakeError("hotkey '{}': {}", name, hotkey.GetError().message);
		}
		value.keys[name] = std::move(*hotkey);
	}
	return std::nullopt;
}

// The accessors of each field. A lambda with no capture becomes a plain function pointer.
#define MOA_TOGGLE(path) \
	.getToggle = [](const LoaderSettings& s) { return s.path; }, .setToggle = [](LoaderSettings& s, bool v) { s.path = v; }
#define MOA_NUMBER(path) \
	.getNumber = [](const LoaderSettings& s) { return s.path; }, .setNumber = [](LoaderSettings& s, double v) { s.path = v; }

const std::array k_Fields {
    Field<LoaderSettings> {.group = "Hotkeys",
                           .section = "hotkeys",
                           .key = "enabled",
                           .label = "Hotkeys on",
                           .help = "Read the hotkeys of the mods in each frame.",
                           MOA_TOGGLE(hotkeys.enabled)},
    Field<LoaderSettings> {.group = "Hotkeys",
                           .section = "hotkeys",
                           .key = "messages",
                           .label = "Messages",
                           .help = "Show a short message of the game when a hotkey changes something.",
                           MOA_TOGGLE(hotkeys.messages)},
    Field<LoaderSettings> {.group = "Hotkeys",
                           .section = "hotkeys",
                           .key = "messageSeconds",
                           .label = "Message time",
                           .help = "The time that a message stays on the screen.",
                           .kind = FieldKind::Number,
                           .min = 0.5,
                           .max = 30.0,
                           .step = 0.5,
                           .format = "%.1f s",
                           MOA_NUMBER(hotkeys.messageSeconds)},
    Field<LoaderSettings> {.group = "Menu bar",
                           .section = "overlay",
                           .key = "fontScale",
                           .label = "Text size",
                           .help = "The size of the text of this menu bar.",
                           .kind = FieldKind::Number,
                           .min = 0.5,
                           .max = 3.0,
                           .step = 0.05,
                           .format = "%.2f",
                           MOA_NUMBER(overlay.fontScale)},
    Field<LoaderSettings> {.group = "Menu bar",
                           .section = "overlay",
                           .key = "enabled",
                           .label = "Menu bar on",
                           .help = "Install this menu bar when the game starts.",
                           .restartOnly = true,
                           MOA_TOGGLE(overlay.enabled)},
    Field<LoaderSettings> {.group = "Start",
                           .section = "",
                           .key = "liveReload",
                           .label = "Live settings",
                           .help = "Watch mods.json and read the hotkeys while the game runs.",
                           .restartOnly = true,
                           MOA_TOGGLE(liveReload)},
    Field<LoaderSettings> {.group = "Start",
                           .section = "",
                           .key = "frameHook",
                           .label = "Frame heartbeat",
                           .help = "Write a heartbeat line to the log of the loader.",
                           .restartOnly = true,
                           MOA_TOGGLE(frameHook)},
};

#undef MOA_TOGGLE
#undef MOA_NUMBER
} // namespace

moa::Result<LoaderSettings> ParseLoaderSettings(std::string_view json)
{
	const auto root = nlohmann::json::parse(json, nullptr, false);
	if (root.is_discarded() || !root.is_object())
	{
		return moa::MakeError("the mod settings are not a JSON object");
	}
	LoaderSettings settings;
	if (!ReadField(root, "frameHook", settings.frameHook))
	{
		return moa::MakeError("the field 'frameHook' must be true or false");
	}
	if (!ReadField(root, "liveReload", settings.liveReload))
	{
		return moa::MakeError("the field 'liveReload' must be true or false");
	}
	if (const auto overlay = root.find("overlay"); overlay != root.end())
	{
		auto& value = settings.overlay;
		if (!overlay->is_object() || !ReadField(*overlay, "enabled", value.enabled) ||
		    !ReadField(*overlay, "fontScale", value.fontScale))
		{
			return moa::MakeError("the object 'overlay' has a field with a wrong type");
		}
		if (value.fontScale < 0.5 || value.fontScale > 3.0)
		{
			return moa::MakeError("'fontScale' must be 0.5 to 3");
		}
	}
	if (const auto objects = root.find("objects"); objects != root.end())
	{
		auto& value = settings.objects;
		if (!objects->is_object() || !ReadField(*objects, "enabled", value.enabled) ||
		    !ReadField(*objects, "trace", value.trace) || !ReadField(*objects, "traceFolder", value.traceFolder) ||
		    !ReadField(*objects, "testClone", value.testClone) || !ReadField(*objects, "packsFolder", value.packsFolder))
		{
			return moa::MakeError("the object 'objects' has a field with a wrong type");
		}
	}
	if (const auto maps = root.find("maps"); maps != root.end())
	{
		auto& value = settings.maps;
		if (!maps->is_object() || !ReadField(*maps, "active", value.active))
		{
			return moa::MakeError("the object 'maps' has a field with a wrong type");
		}
		if (!value.active.empty() && !moa::mapoverride::IsValidMapName(value.active))
		{
			return moa::MakeError("'maps.active' must be empty or a name of letters, digits, '-', '_' and '.'");
		}
	}
	if (const auto mods = root.find("mods"); mods != root.end())
	{
		if (auto error = ReadMods(*mods, settings.mods))
		{
			return *error;
		}
	}
	if (const auto dlls = root.find("modDlls"); dlls != root.end())
	{
		if (auto error = ReadModDlls(*dlls, settings.modDlls))
		{
			return *error;
		}
	}
	if (const auto hotkeys = root.find("hotkeys"); hotkeys != root.end())
	{
		if (auto error = ReadHotkeys(*hotkeys, settings.hotkeys))
		{
			return *error;
		}
	}
	return settings;
}

nlohmann::json LoaderSettingsJson(const LoaderSettings& settings)
{
	nlohmann::json hotkeys {
	    {"enabled", settings.hotkeys.enabled},
	    {"messages", settings.hotkeys.messages},
	    {"messageSeconds", settings.hotkeys.messageSeconds},
	};
	for (const auto& [name, hotkey] : settings.hotkeys.keys)
	{
		hotkeys[name] = FormatHotkey(hotkey);
	}
	nlohmann::json mods {{"disabled", settings.mods.disabled}};
	if (settings.mods.map)
	{
		mods["map"] = *settings.mods.map;
	}
	return {
	    {"frameHook", settings.frameHook},
	    {"liveReload", settings.liveReload},
	    {"hotkeys", std::move(hotkeys)},
	    {"overlay", {{"enabled", settings.overlay.enabled}, {"fontScale", settings.overlay.fontScale}}},
	    {"objects",
	     {
	         {"enabled", settings.objects.enabled},
	         {"trace", settings.objects.trace},
	         {"traceFolder", settings.objects.traceFolder},
	         {"testClone", settings.objects.testClone},
	         {"packsFolder", settings.objects.packsFolder},
	     }},
	    {"maps", {{"active", settings.maps.active}}},
	    {"mods", std::move(mods)},
	    {"modDlls", {{"disabled", settings.modDlls.disabled}}},
	};
}

nlohmann::json LoaderSettingsChange::Describe() const
{
	return {{"hotkeys", hotkeys}, {"overlay", overlay}, {"restartOnly", restartOnly}};
}

LoaderSettingsChange CompareLoaderSettings(const LoaderSettings& current, const LoaderSettings& next)
{
	LoaderSettingsChange change;
	change.hotkeys = current.hotkeys != next.hotkeys;
	change.overlay = current.overlay.fontScale != next.overlay.fontScale;
	const std::array<std::pair<const char*, bool>, 7> restart {{
	    {"frameHook", current.frameHook != next.frameHook},
	    {"liveReload", current.liveReload != next.liveReload},
	    {"overlay.enabled", current.overlay.enabled != next.overlay.enabled},
	    {"objects", current.objects != next.objects},
	    {"maps.active", current.maps != next.maps},
	    {"mods", current.mods != next.mods},
	    {"modDlls", current.modDlls != next.modDlls},
	}};
	for (const auto& [name, changed] : restart)
	{
		if (changed)
		{
			change.restartOnly.emplace_back(name);
		}
	}
	return change;
}

LoaderSettings LiveLoaderSettings(const LoaderSettings& current, const LoaderSettings& next)
{
	auto live = next;
	live.frameHook = current.frameHook;
	live.liveReload = current.liveReload;
	live.overlay.enabled = current.overlay.enabled;
	live.objects = current.objects;
	live.maps = current.maps;
	live.mods = current.mods;
	live.modDlls = current.modDlls;
	return live;
}

std::span<const settings::Field<LoaderSettings>> LoaderFields()
{
	return k_Fields;
}

moa::Result<settings::SettingsEdit> EditLoaderField(LoaderSettings& current, const settings::Field<LoaderSettings>& field,
                                                    const settings::FieldValue& value)
{
	return settings::EditField(current, field, value, settings::FieldRange {.min = field.min, .max = field.max},
	                           [](const LoaderSettings& next) -> moa::Result<void> {
		                           if (auto checked = ParseLoaderSettings(LoaderSettingsJson(next).dump()); !checked)
		                           {
			                           return checked.GetError();
		                           }
		                           return {};
	                           });
}

} // namespace moa_mod::loader
