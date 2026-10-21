/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "game/overlay/OverlayUi.h"

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <string_view>

#include <core/ModCatalogue.h>
#include <imgui.h>
#include <mod_core/Hotkey.h>

namespace moa_mod::overlay
{

namespace
{
/// The width of the controls in a menu, in multiples of the font size.
constexpr float k_ControlWidth = 14.0f;

[[nodiscard]] std::string Text(const nlohmann::json& object, const char* key)
{
	if (!object.is_object() || !object.contains(key))
	{
		return {};
	}
	const auto& value = object.at(key);
	return value.is_string() ? value.get<std::string>() : value.dump();
}

[[nodiscard]] bool Flag(const nlohmann::json& object, const char* key)
{
	return object.is_object() && object.contains(key) && object.at(key).is_boolean() && object.at(key).get<bool>();
}

void Tooltip(const MenuItem& item)
{
	if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
	{
		return;
	}
	std::string text = item.help;
	if (!item.path.empty())
	{
		text += "\n" + item.path;
	}
	if (item.restartOnly)
	{
		text += "\nThe change works after a restart of the game.";
		text += item.state.restartPending != 0 ? "\nA restart is necessary now." : "";
	}
	if (!item.hotkey.empty())
	{
		text += "\nHotkey: " + item.hotkey;
	}
	if (!text.empty())
	{
		ImGui::SetTooltip("%s", text.c_str());
	}
}

/// Change the field in the draft and give the command to the main thread.
void Edit(const OverlayUiFrame& frame, const MenuItem& item, const MoaFieldValue& value, bool save)
{
	frame.draft[item.id] = value;
	frame.post(OverlayCommand {.kind = OverlayCommand::Kind::Edit, .id = item.id, .value = value, .save = save});
}

void DrawField(const OverlayUiFrame& frame, const MenuItem& item)
{
	ImGui::PushID(static_cast<int>(item.id));
	auto value = frame.draft.contains(item.id) ? frame.draft.at(item.id) : item.state.value;
	auto label = item.label;
	if (item.restartOnly)
	{
		label += item.state.restartPending != 0 ? " (restart now)" : " (restart)";
	}
	switch (item.fieldKind)
	{
	case MOA_FIELD_TOGGLE:
	{
		bool on = value.toggle != 0;
		if (ImGui::Checkbox(label.c_str(), &on))
		{
			value.toggle = on ? 1u : 0u;
			Edit(frame, item, value, true);
		}
		break;
	}
	case MOA_FIELD_NUMBER:
	{
		double number = value.number;
		const double min = item.state.min;
		const double max = item.state.max;
		ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp;
		if (item.logarithmic)
		{
			flags |= ImGuiSliderFlags_Logarithmic;
		}
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * k_ControlWidth);
		const auto* format = item.format.empty() ? "%.2f" : item.format.c_str();
		if (ImGui::SliderScalar(label.c_str(), ImGuiDataType_Double, &number, &min, &max, format, flags))
		{
			// The mods use the value at once; mods.json gets it when the user releases the slider.
			value.number = number;
			Edit(frame, item, value, false);
		}
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			Edit(frame, item, frame.draft[item.id], true);
		}
		break;
	}
	case MOA_FIELD_CHOICE:
	{
		const auto current = static_cast<size_t>(value.choice);
		const auto* preview = current < item.choices.size() ? item.choices[current].c_str() : "";
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * k_ControlWidth);
		if (ImGui::BeginCombo(label.c_str(), preview))
		{
			for (size_t index = 0; index < item.choices.size(); ++index)
			{
				if (ImGui::Selectable(item.choices[index].c_str(), index == current) && index != current)
				{
					value.choice = static_cast<uint32_t>(index);
					Edit(frame, item, value, true);
				}
			}
			ImGui::EndCombo();
		}
		break;
	}
	}
	Tooltip(item);
	ImGui::PopID();
}

void DrawAction(const OverlayUiFrame& frame, const MenuItem& item)
{
	ImGui::PushID(static_cast<int>(item.id));
	ImGui::BeginDisabled(!item.enabled);
	if (ImGui::Button(item.label.c_str()))
	{
		frame.post(OverlayCommand {.kind = OverlayCommand::Kind::Action, .id = item.id});
	}
	ImGui::EndDisabled();
	Tooltip(item);
	ImGui::PopID();
}

/// Draw the items of one menu, with a heading for each group.
void DrawItems(const OverlayUiFrame& frame, const std::vector<MenuItem>& items)
{
	std::string group;
	for (const auto& item : items)
	{
		if (item.group != group)
		{
			group = item.group;
			if (!group.empty())
			{
				ImGui::SeparatorText(group.c_str());
			}
		}
		switch (item.kind)
		{
		case MenuItem::Kind::Field:
			DrawField(frame, item);
			break;
		case MenuItem::Kind::Action:
			DrawAction(frame, item);
			break;
		case MenuItem::Kind::Readout:
			ImGui::TextUnformatted(item.text.c_str());
			break;
		}
	}
}

void DrawModMenus(const OverlayUiFrame& frame)
{
	for (const auto& menu : frame.snapshot.menus)
	{
		if (ImGui::BeginMenu(menu.name.c_str()))
		{
			DrawItems(frame, menu.items);
			ImGui::EndMenu();
		}
	}
}

void DrawStatus(const OverlayUiFrame& frame)
{
	ImGui::SeparatorText("Status");
	if (frame.snapshot.mods.is_array())
	{
		for (const auto& mod : frame.snapshot.mods)
		{
			ImGui::Text("%s: %s, %s, %s", Text(mod, "name").c_str(), Flag(mod, "enabled") ? "on" : "off",
			            Flag(mod, "ready") ? "ready" : "not ready", Flag(mod, "active") ? "active" : "not active");
			if (const auto reason = Text(mod, "standBackReason"); !reason.empty())
			{
				ImGui::TextDisabled("  stands back: %s", reason.c_str());
			}
			if (const auto error = Text(mod, "lastError"); !error.empty())
			{
				ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "  last error: %s", error.c_str());
			}
		}
	}
	if (frame.snapshot.hooks.is_object())
	{
		for (const auto& [name, hook] : frame.snapshot.hooks.items())
		{
			ImGui::Text("%s: %s, %s calls", name.c_str(), Flag(hook, "installed") ? "installed" : "not installed",
			            Text(hook, "calls").empty() ? "0" : Text(hook, "calls").c_str());
		}
	}
	const auto& status = frame.overlayStatus;
	ImGui::Text("Menu bar: %s, %s frames drawn", Text(status, "api").c_str(), Text(status, "framesDrawn").c_str());
	if (const auto error = Text(status, "lastError"); !error.empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Menu bar error: %s", error.c_str());
	}
	const auto& reload = frame.snapshot.lastReload;
	if (reload.is_object() && !reload.empty())
	{
		ImGui::Text("Last reload: %s (%s), %s", Text(reload, "time").c_str(), Text(reload, "source").c_str(),
		            Flag(reload, "applied") ? "applied" : "not applied");
		if (const auto error = Text(reload, "error"); !error.empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "  error: %s", error.c_str());
		}
	}
}

void DrawModsMenu(const OverlayUiFrame& frame)
{
	if (!ImGui::BeginMenu("Mods"))
	{
		return;
	}
	if (ImGui::MenuItem("Reload mods.json"))
	{
		frame.post(OverlayCommand {.kind = OverlayCommand::Kind::Reload});
	}
	ImGui::TextDisabled("Settings file:");
	ImGui::TextUnformatted(frame.snapshot.settingsPath.c_str());
	if (ImGui::SmallButton("Copy the path"))
	{
		ImGui::SetClipboardText(frame.snapshot.settingsPath.c_str());
	}
	DrawStatus(frame);
	DrawItems(frame, frame.snapshot.loaderItems);
	ImGui::SeparatorText("Hotkeys");
	if (!frame.snapshot.hotkeysReady)
	{
		ImGui::TextDisabled("The hotkeys are not ready (see the log).");
	}
	if (ImGui::BeginTable("hotkeys", 3, ImGuiTableFlags_SizingFixedFit))
	{
		for (const auto& row : frame.snapshot.hotkeys)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(row.name.c_str());
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(row.hotkey.c_str());
			ImGui::TableNextColumn();
			ImGui::TextDisabled("%s", row.owner.c_str());
		}
		ImGui::EndTable();
	}
	ImGui::EndMenu();
}

/// The choice of the map in the settings that the user asked for.
[[nodiscard]] mods::MapChoice DraftMapChoice(const loader::LoaderSettings& settings)
{
	using Kind = mods::MapChoice::Kind;
	if (!settings.maps.active.empty())
	{
		return {.kind = Kind::Folder, .name = settings.maps.active};
	}
	if (!settings.mods.map)
	{
		return {.kind = Kind::Automatic};
	}
	return settings.mods.map->empty() ? mods::MapChoice {} : mods::MapChoice {.kind = Kind::Mod, .name = *settings.mods.map};
}

void MapRadio(const OverlayUiFrame& frame, const char* label, const mods::MapChoice& choice, const char* help)
{
	const auto current = DraftMapChoice(frame.draftLoader);
	if (ImGui::RadioButton(label, current == choice) && current != choice)
	{
		if (mods::EditMapChoice(frame.draftLoader, choice))
		{
			frame.post(OverlayCommand {.kind = OverlayCommand::Kind::Map, .map = choice});
		}
	}
	if (help != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
	{
		ImGui::SetTooltip("%s", help);
	}
}

/// The tooltip of a mod: what `mod.json` says about it.
void ModTooltip(const nlohmann::json& mod)
{
	if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled) || !ImGui::BeginTooltip())
	{
		return;
	}
	ImGui::Text("File: %s", Text(mod, "file").c_str());
	if (mod.contains("id"))
	{
		ImGui::Text("Id: %s, version %s", Text(mod, "id").c_str(), Text(mod, "version").c_str());
		if (const auto author = Text(mod, "author"); !author.empty())
		{
			ImGui::Text("Author: %s", author.c_str());
		}
		if (const auto description = Text(mod, "description"); !description.empty())
		{
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
			ImGui::TextUnformatted(description.c_str());
			ImGui::PopTextWrapPos();
		}
		std::string contents = Flag(mod, "map") ? "a map" : "";
		for (const auto& pack : mod.value("packs", nlohmann::json::array()))
		{
			contents += (contents.empty() ? "the pack " : ", the pack ") + pack.get<std::string>();
		}
		ImGui::Text("Contents: %s", contents.c_str());
		ImGui::Text("Game builds: %s", mod.value("buildGuids", nlohmann::json::array()).dump().c_str());
	}
	ImGui::EndTooltip();
}

/// The mod zips in the folder `mods` of the game: a switch for each mod, its status, and the choice of the map.
void DrawModListMenu(const OverlayUiFrame& frame)
{
	if (!ImGui::BeginMenu("Mod list"))
	{
		return;
	}
	const ImVec4 errorColour(1.0f, 0.5f, 0.4f, 1.0f);
	const auto& list = frame.snapshot.modList;
	ImGui::TextDisabled("The mod zips and the mod DLLs in:");
	ImGui::TextUnformatted(frame.snapshot.modsFolder.c_str());
	ImGui::TextDisabled("Each zip and DLL loads, except one that you turn off. No game file changes.");
	ImGui::TextDisabled("A change works after a restart of the game.");
	if (frame.draftLoader.mods != frame.snapshot.settings.mods || frame.draftLoader.maps != frame.snapshot.settings.maps ||
	    frame.draftLoader.modDlls != frame.snapshot.settings.modDlls)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Restart the game to use the changes.");
	}
	ImGui::SeparatorText("Mod zips");
	const auto mods = list.is_object() ? list.value("mods", nlohmann::json::array()) : nlohmann::json::array();
	if (mods.empty())
	{
		ImGui::TextDisabled("No mod zips. Put a zip into the folder above.");
	}
	for (size_t index = 0; index < mods.size(); ++index)
	{
		const auto& mod = mods[index];
		const auto id = Text(mod, "id");
		const auto state = Text(mod, "state");
		ImGui::PushID(static_cast<int>(index));
		bool on =
		    !id.empty() && std::ranges::find(frame.draftLoader.mods.disabled, id) == frame.draftLoader.mods.disabled.end();
		const auto label = id.empty() ? Text(mod, "file") : std::format("{} {}", Text(mod, "name"), Text(mod, "version"));
		ImGui::BeginDisabled(id.empty() || state == "refused");
		if (ImGui::Checkbox(label.c_str(), &on) && mods::EditModEnabled(frame.draftLoader, id, on))
		{
			frame.post(OverlayCommand {.kind = OverlayCommand::Kind::Mod, .on = on, .name = id});
		}
		ImGui::EndDisabled();
		ModTooltip(mod);
		ImGui::SameLine();
		ImGui::TextDisabled("(%s%s)", state.c_str(), Flag(mod, "mapActive") ? ", its map loads" : "");
		if (const auto error = Text(mod, "error"); !error.empty())
		{
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
			ImGui::TextColored(errorColour, "  %s", error.c_str());
			ImGui::PopTextWrapPos();
		}
		for (const auto& note : mod.value("notes", nlohmann::json::array()))
		{
			const auto text = "  " + Text(note, "text");
			if (Flag(note, "error"))
			{
				ImGui::TextColored(errorColour, "%s", text.c_str());
			}
			else
			{
				ImGui::TextDisabled("%s", text.c_str());
			}
		}
		ImGui::PopID();
	}

	ImGui::SeparatorText("Mod DLLs");
	const auto dlls = frame.snapshot.dllList.is_array() ? frame.snapshot.dllList : nlohmann::json::array();
	if (dlls.empty())
	{
		ImGui::TextDisabled("No mod DLLs. Put a mod DLL into the folder above.");
	}
	for (size_t index = 0; index < dlls.size(); ++index)
	{
		const auto& dll = dlls[index];
		const auto file = Text(dll, "file");
		const auto& disabled = frame.draftLoader.modDlls.disabled;
		bool on = std::ranges::find(disabled, file) == disabled.end();
		ImGui::PushID(static_cast<int>(1000 + index));
		const auto label =
		    Text(dll, "name").empty() ? file : std::format("{} {} ({})", Text(dll, "name"), Text(dll, "version"), file);
		if (ImGui::Checkbox(label.c_str(), &on))
		{
			auto& disabledList = frame.draftLoader.modDlls.disabled;
			std::erase(disabledList, file);
			if (!on)
			{
				disabledList.push_back(file);
			}
			frame.post(OverlayCommand {.kind = OverlayCommand::Kind::ModDll, .on = on, .name = file});
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(%s)", Text(dll, "state").c_str());
		if (const auto error = Text(dll, "error"); !error.empty())
		{
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
			ImGui::TextColored(errorColour, "  %s", error.c_str());
			ImGui::PopTextWrapPos();
		}
		ImGui::PopID();
	}

	ImGui::SeparatorText("Map override (only one)");
	using Kind = mods::MapChoice::Kind;
	MapRadio(frame, "Automatic", mods::MapChoice {.kind = Kind::Automatic},
	         "The map of the first mod with a map, in the order of the mod ids.");
	MapRadio(frame, "None", mods::MapChoice {}, "The game loads its own map.");
	for (const auto& mod : mods)
	{
		if (Flag(mod, "map") && Text(mod, "state") != "refused")
		{
			const auto label = std::format("Mod: {}##{}", Text(mod, "name"), Text(mod, "id"));
			MapRadio(frame, label.c_str(), mods::MapChoice {.kind = Kind::Mod, .name = Text(mod, "id")}, nullptr);
		}
	}
	for (const auto& name : frame.snapshot.maps)
	{
		const auto label = std::format("Folder: {}##folder", name);
		MapRadio(frame, label.c_str(), mods::MapChoice {.kind = Kind::Folder, .name = name},
		         "A map override in the development folder of the maps (loose files from the editor).");
	}
	if (list.is_object() && list.contains("map"))
	{
		const auto& map = list.at("map");
		const auto kind = Text(map, "kind");
		ImGui::Text("Now: %s", kind == "none" ? "no map override" : std::format("the {} {}", kind, Text(map, "name")).c_str());
		if (const auto note = Text(list, "mapNote"); !note.empty())
		{
			ImGui::TextColored(errorColour, "  %s", note.c_str());
		}
	}
	ImGui::EndMenu();
}

void DrawHelpMenu(const OverlayUiFrame& frame)
{
	if (!ImGui::BeginMenu("Help"))
	{
		return;
	}
	ImGui::Text("Masters of Albion mod loader %s", frame.snapshot.version.c_str());
	ImGui::Text("Game build: %s", frame.snapshot.buildGuid.c_str());
	ImGui::Text("Renderer: %s", Text(frame.overlayStatus, "api").c_str());
	ImGui::Text("Dear ImGui %s", IMGUI_VERSION);
	ImGui::Separator();
	const auto pin = std::ranges::find(frame.snapshot.hotkeys, std::string("toggleOverlay"), &HotkeyRow::name);
	const auto pinText = pin != frame.snapshot.hotkeys.end() ? pin->hotkey : std::string("the hotkey toggleOverlay");
	ImGui::TextUnformatted("Escape opens the menu of the game and this bar.");
	ImGui::Text("%s keeps the bar open while the game runs, or closes it.", pinText.c_str());
	ImGui::TextUnformatted("A change works at once and goes into mods.json.");
	ImGui::TextUnformatted("A field with (restart) works after a restart of the game.");
	ImGui::EndMenu();
}
} // namespace

void DrawOverlayMenuBar(const OverlayUiFrame& frame)
{
	if (!ImGui::BeginMainMenuBar())
	{
		return;
	}
	DrawModMenus(frame);
	DrawModListMenu(frame);
	DrawModsMenu(frame);
	DrawHelpMenu(frame);
	const auto summary =
	    std::format("{} mods{}", frame.snapshot.menus.size(), Flag(frame.overlayStatus, "pinned") ? "  (pinned)" : "");
	ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(summary.c_str()).x - ImGui::GetStyle().ItemSpacing.x * 2.0f);
	ImGui::TextDisabled("%s", summary.c_str());
	ImGui::EndMainMenuBar();
}

} // namespace moa_mod::overlay
