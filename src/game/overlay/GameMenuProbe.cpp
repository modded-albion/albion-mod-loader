/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "game/overlay/GameMenuProbe.h"

namespace moa_mod::overlay
{

moa::Result<GameMenuProbe> GameMenuProbe::Bind(const Managed& managed)
{
	Binder binder(managed);
	GameMenuProbe probe(managed);
	auto* gameManager = binder.Class("", "GameManager");
	auto* controller = binder.Class("", "BaseUIController");
	auto* unityObject = binder.Class("UnityEngine", "Object");
	probe._settingsPane = binder.Field(gameManager, "m_settingsPane", "SettingsUIController");
	probe._isClosing = binder.Field(controller, "isClosing", "System.Boolean");
	// The implicit conversion of a Unity object to bool is false for an object that Unity destroyed.
	probe._isAlive = binder.Method(unityObject, "op_Implicit", {"UnityEngine.Object"}, "System.Boolean", true);
	if (!binder.Errors().empty())
	{
		return moa::MakeError("the probe of the game menu: {}", binder.Errors());
	}
	return probe;
}

bool GameMenuProbe::IsOpen(void* gameManager) const
{
	if (gameManager == nullptr)
	{
		return false;
	}
	auto* pane = ReadField<Il2CppObject*>(gameManager, _settingsPane);
	if (pane == nullptr)
	{
		return false;
	}
	const auto alive = _managed->Call<bool>(_isAlive, nullptr, {pane});
	if (!alive || !*alive)
	{
		return false;
	}
	return !ReadField<bool>(pane, _isClosing);
}

} // namespace moa_mod::overlay
