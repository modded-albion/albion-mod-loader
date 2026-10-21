/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstddef>

#include <common/Result.h>

#include "mod_game/Managed.h"

namespace moa_mod::overlay
{

/// Tells if the menu of the game is open. The Escape key opens the settings pane of the game
/// (`SettingsUIController`), and `GameManager.m_settingsPane` keeps it while it is open. The probe reads the names at
/// the start and the pane on the main thread.
class GameMenuProbe
{
public:
	/// Find the names. Return an error that names the missing ones.
	[[nodiscard]] static moa::Result<GameMenuProbe> Bind(const Managed& managed);

	/// Return true if the settings pane of the game is open and does not close. Call it only on the main thread, with
	/// the `GameManager` of the frame. A null `GameManager` gives false.
	[[nodiscard]] bool IsOpen(void* gameManager) const;

private:
	explicit GameMenuProbe(const Managed& managed)
	    : _managed(&managed)
	{
	}

	const Managed* _managed;
	size_t _settingsPane = 0;
	size_t _isClosing = 0;
	const MethodInfo* _isAlive = nullptr;
};

} // namespace moa_mod::overlay
