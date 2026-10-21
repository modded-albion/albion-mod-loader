/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstddef>

#include <functional>
#include <map>

#include <moa_mod/api/ModApi.h>
#include <moa_mod/loader/LoaderSettings.h>
#include <nlohmann/json.hpp>

#include "moa_mod/overlay/Overlay.h"

namespace moa_mod::overlay
{

/// What the menu bar draws in one frame.
struct OverlayUiFrame
{
	const OverlaySnapshot& snapshot;
	/// The values that the controls show and change, by the id of the field. A change goes to `post` too.
	std::map<size_t, MoaFieldValue>& draft;
	/// The settings of the loader that the mod list and the map choice change.
	loader::LoaderSettings& draftLoader;
	const std::function<void(OverlayCommand)>& post;
	/// The status of the menu bar itself (`Overlay::Describe`).
	const nlohmann::json& overlayStatus;
};

/// Draw the main menu bar: a menu for each mod, the menus Mod list and Mods, and Help. Call it between
/// `ImGui::NewFrame` and `ImGui::Render`.
void DrawOverlayMenuBar(const OverlayUiFrame& frame);

} // namespace moa_mod::overlay
