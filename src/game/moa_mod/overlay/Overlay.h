/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <moa/common/Result.h>
#include <moa_mod/api/ModApi.h>
#include <moa_mod/loader/LoaderSettings.h>
#include <moa_mod/mods/ModCatalogue.h>
#include <moa_mod/overlay/OverlayRules.h>
#include <nlohmann/json.hpp>
#include <windows.h>

#include "moa_mod/overlay/DxgiHooks.h"

struct ImGuiContext;

namespace moa_mod::overlay
{

class OverlayRenderer;

/// One control or line of a menu, as the main thread read it from the registrations of the loader and the mods.
struct MenuItem
{
	enum class Kind
	{
		Field,   ///< A value of the settings.
		Action,  ///< A button.
		Readout, ///< Lines of text.
	};
	Kind kind = Kind::Field;
	size_t id = 0;
	std::string group {};
	std::string label {};
	std::string help {};
	/// Fields: the path in mods.json, the control, the range and the names of a choice.
	std::string path {};
	MoaFieldKind fieldKind = MOA_FIELD_TOGGLE;
	std::string format {};
	bool logarithmic = false;
	bool restartOnly = false;
	std::vector<std::string> choices {};
	/// Fields: the value that the user asked for and the range now.
	MoaFieldState state {};
	/// Actions: the button can be pressed now, and the text of its hotkey.
	bool enabled = true;
	std::string hotkey {};
	/// Readouts: the text.
	std::string text {};
};

/// One menu of the menu bar: the menu of a mod, or a menu that a mod named.
struct Menu
{
	std::string name {};
	std::vector<MenuItem> items {};
};

/// A hotkey of an action, for the list in the menu Mods.
struct HotkeyRow
{
	std::string name {};
	std::string hotkey {};
	std::string owner {};
};

/// What the menu bar shows of the mods. The main thread makes it and gives it to the menu bar.
struct OverlaySnapshot
{
	loader::LoaderSettings settings {};   ///< The settings that the loader uses now.
	loader::LoaderSettings requested {};  ///< The settings that the user asked for. A field that needs a restart can differ.
	std::vector<Menu> menus {};           ///< The menus of the mods, in the order of the mods.
	std::vector<MenuItem> loaderItems {}; ///< The fields of the loader for the menu Mods.
	nlohmann::json mods {};               ///< The status of each mod DLL and of each part of the loader.
	nlohmann::json hooks {};              ///< The status of the hooks.
	nlohmann::json lastReload {};
	std::vector<HotkeyRow> hotkeys {};
	std::string settingsPath {};
	std::string buildGuid {};
	std::string version {};
	bool live = false;
	bool hotkeysReady = false;
	std::vector<std::string> maps {}; ///< The map overrides in the folder of the overrides.
	/// The mod zips (`DescribeCatalogue`), with the status of the map and the packs of each mod that loads.
	nlohmann::json modList {};
	/// The mod DLLs in the folder `mods`: file, id, name, version, state and error.
	nlohmann::json dllList {};
	std::string modsFolder {}; ///< The folder of the mod zips and the mod DLLs in the game folder.
};

/// A change that the user makes in the menu bar. The main thread does it.
struct OverlayCommand
{
	enum class Kind
	{
		Edit,   ///< Change one field (`id`, `value`).
		Reload, ///< Read mods.json again.
		Action, ///< Do an action (`id`).
		Mod,    ///< Turn the mod zip `name` on or off (`on`).
		Map,    ///< Choose the map override `map`.
		ModDll, ///< Turn the mod DLL `name` on or off (`on`).
	};
	Kind kind = Kind::Edit;
	size_t id = 0;
	MoaFieldValue value {};
	bool save = false; ///< Write the change to mods.json. A slider writes only when the user releases it.
	bool on = false;
	std::string name {};
	mods::MapChoice map {};
};

/// What the loader gives to the menu bar.
struct OverlayHost
{
	/// Run work on the main thread of the game, between two frames.
	std::function<void(std::function<void()>)> runOnMainThread;
	/// Do a command. The menu bar calls it through `runOnMainThread`.
	std::function<void(const OverlayCommand&)> apply;
};

/// The menu bar of the mods: a Dear ImGui main menu bar at the top of the game window.
///
/// Threads:
/// - The main thread calls `Tick` and `Publish` in the frame hook.
/// - The render thread of the game draws the bar in the hook on `Present`.
/// - The thread of the game window gives the input to the bar in the window procedure.
/// One lock keeps the ImGui context. Any error stops only the menu bar; the game continues.
class Overlay final: public SwapChainListener
{
public:
	/// Install the hooks on the swap chain. Only one menu bar can exist.
	[[nodiscard]] static moa::Result<std::unique_ptr<Overlay>> Install(OverlayHost host);

	~Overlay();
	Overlay(const Overlay&) = delete;
	Overlay& operator=(const Overlay&) = delete;

	/// Calculate the visible state of this frame. Call it on the main thread, one time in each frame.
	VisibilityChange Tick(bool menuOpen, bool togglePin);
	/// Give the menu bar the newest state of the mods. Call it on the main thread.
	void Publish(OverlaySnapshot snapshot);
	/// Remove the window procedure and the hooks. `DllMain` can call it. The menu bar then never shows again.
	void Shutdown();

	/// Give a command to the main thread, as a control of the bar does. Any thread can call it.
	void Post(OverlayCommand command);

	[[nodiscard]] bool Visible() const { return _visible.load(); }
	[[nodiscard]] nlohmann::json Describe() const;

	/// The window procedure calls it for each message of the game window.
	[[nodiscard]] InputDecision OnWindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;

	void BeforePresent(IDXGISwapChain* swapChain, UINT flags) noexcept override;
	void AfterPresent(IDXGISwapChain* swapChain, HRESULT result) noexcept override;
	void BeforeResizeBuffers(IDXGISwapChain* swapChain) noexcept override;

private:
	explicit Overlay(OverlayHost host);

	/// Stop the menu bar after an error. The hooks and the window procedure stay, but they do nothing.
	void Fail(const std::string& reason) noexcept;
	void SetErrorText(const std::string& error);
	/// Bind the menu bar to the swap chain of the game window. Return false if this swap chain does not draw the bar.
	[[nodiscard]] bool Bind(IDXGISwapChain* swapChain);
	void Present(IDXGISwapChain* swapChain);
	/// Tell in the log which graphics API the swap chain of the first frame uses.
	void LogFirstPresent(IDXGISwapChain* swapChain) noexcept;
	void DrawFrame(IDXGISwapChain* swapChain);
	void SubclassWindow(HWND window);
	void RestoreWindow();

	OverlayHost _host;
	std::unique_ptr<DxgiHooks> _hooks;

	/// Keeps the ImGui context, the renderer and the window. A thread that waits longer than a short time does nothing.
	std::timed_mutex _mutex;
	ImGuiContext* _context = nullptr;
	std::unique_ptr<OverlayRenderer> _renderer;
	std::atomic<IDXGISwapChain*> _swapChain = nullptr;
	HWND _window = nullptr;
	std::atomic<bool> _rebuild = false;

	std::atomic<bool> _visible = false;
	std::atomic<bool> _pinned = false;
	std::atomic<bool> _failed = false;
	std::atomic<uint64_t> _presentCalls = 0;
	std::atomic<uint64_t> _framesDrawn = 0;
	std::atomic<uint64_t> _mainFrames = 0;
	bool _noFramesLogged = false;

	/// Main thread only.
	OverlayVisibility _visibility;

	std::mutex _snapshotMutex;
	OverlaySnapshot _snapshot;
	uint64_t _snapshotRevision = 0;

	/// Render thread only: the copy of the snapshot that the bar draws, and the settings that the controls change.
	OverlaySnapshot _shown;
	uint64_t _shownRevision = 0;
	/// The values that the controls show, by the id of the field.
	std::map<size_t, MoaFieldValue> _draft;
	loader::LoaderSettings _draftLoader;
	std::atomic<int> _pendingEdits = 0;

	mutable std::mutex _statusMutex;
	std::string _api;
	std::string _lastError;
};

} // namespace moa_mod::overlay
