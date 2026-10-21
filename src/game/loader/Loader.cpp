/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include <core/HostTable.h>
#include <core/LoaderSettings.h>
#include <core/ModCatalogue.h>
#include <core/ModRegistry.h>
#include <dlls/ModDllLoader.h>
#include <mapoverride/MapOverride.h>
#include <mod_core/Folders.h>
#include <mod_core/MainThreadTasks.h>
#include <mod_core/SettingsFile.h>
#include <mod_core/StartupGuard.h>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <windows.h>

#include "game/maps/MapsMod.h"
#include "game/objects/ObjectsMod.h"
#include "game/overlay/DxgiHooks.h"
#include "game/overlay/GameMenuProbe.h"
#include "game/overlay/Overlay.h"
#include "game/proxy/VersionExports.h"
#include "mod_game/CrashDump.h"
#include "mod_game/GameMessages.h"
#include "mod_game/Hooks.h"
#include "mod_game/HotkeyInput.h"
#include "mod_game/Il2Cpp.h"
#include "mod_game/Managed.h"
#include "mod_game/ModStatus.h"

// ProxyStubs.asm jumps through this table to the functions of the system version.dll.
extern "C" void* g_versionFunctions[moa::proxy::k_VersionExports.size()] = {};

/// The install tool looks for this export to know that a version.dll is the mod loader.
extern "C" const char* MoaModLoaderVersion()
{
	return "0.1.0";
}

namespace moa_mod::loader
{

using overlay::GameMenuProbe;
using overlay::Menu;
using overlay::MenuItem;
using overlay::Overlay;
using overlay::OverlayCommand;
using overlay::OverlayHost;
using overlay::OverlaySnapshot;

namespace
{
constexpr auto k_StartTimeout = std::chrono::minutes(3);
constexpr auto k_PollInterval = std::chrono::milliseconds(50);
constexpr auto k_SettleTime = std::chrono::seconds(2);
/// The time between two checks of the settings file.
constexpr auto k_SettingsWatchInterval = std::chrono::seconds(1);
/// The shortest time that the list of the settings stays on the screen.
constexpr double k_SummarySeconds = 6.0;
/// The two copies of the game on the computer of the developer. Nothing of the loader writes to them.
constexpr const wchar_t* k_OriginalGameFolder = L"D:/masters_of_Albion_original";
constexpr const wchar_t* k_SteamGameFolder = L"F:/SteamLibrary/steamapps/common/Masters of Albion";
/// The method that the loader runs its frame work after: one call in each frame of the game.
const MethodKey k_FrameMethod {.space = "", .className = "GameManager", .method = "Update", .parameterTypes = {}};

/// Load the system version.dll and fill the table of the proxy stubs.
bool LoadSystemVersion()
{
	std::array<wchar_t, MAX_PATH> folder {};
	const auto length = GetSystemDirectoryW(folder.data(), static_cast<UINT>(folder.size()));
	if (length == 0 || length >= folder.size())
	{
		return false;
	}
	const auto path = std::wstring(folder.data(), length) + L"\\version.dll";
	const auto module = LoadLibraryW(path.c_str());
	if (module == nullptr)
	{
		return false;
	}
	for (size_t i = 0; i < moa::proxy::k_VersionExports.size(); ++i)
	{
		g_versionFunctions[i] = reinterpret_cast<void*>(GetProcAddress(module, moa::proxy::k_VersionExports[i]));
	}
	return true;
}

[[nodiscard]] std::string ReadEnvironment(const char* name, const std::string& fallback)
{
	std::array<char, 1024> value {};
	const auto length = GetEnvironmentVariableA(name, value.data(), static_cast<DWORD>(value.size()));
	return length == 0 || length >= value.size() ? fallback : std::string(value.data(), length);
}

[[nodiscard]] std::filesystem::path ProgramPath()
{
	std::array<wchar_t, MAX_PATH> path {};
	GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
	return path.data();
}

[[nodiscard]] std::filesystem::path GameFolder()
{
	return ProgramPath().parent_path();
}

/// Other programs in the game folder (for example the crash handler) also load version.dll. The loader starts only
/// in the game.
[[nodiscard]] bool IsGameProcess()
{
	return CompareStringOrdinal(ProgramPath().filename().c_str(), -1, L"moa.exe", -1, TRUE) == CSTR_EQUAL;
}

/// Read the build guid from moa_Data/boot.config. A new game build has a new guid.
[[nodiscard]] std::string ReadBuildGuid()
{
	std::ifstream config(GameFolder() / "moa_Data" / "boot.config");
	constexpr std::string_view key = "build-guid=";
	for (std::string line; std::getline(config, line);)
	{
		if (line.starts_with(key))
		{
			return line.substr(key.size());
		}
	}
	return "unknown-build";
}

[[nodiscard]] std::string Utf8(const std::filesystem::path& path)
{
	const auto text = path.u8string();
	return {text.begin(), text.end()};
}

[[nodiscard]] std::filesystem::path PathOf(const std::string& utf8)
{
	return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

/// The folder of the settings of the mods, outside the game folder: the local application data of the user.
[[nodiscard]] std::filesystem::path SettingsFolder()
{
	return std::filesystem::path(ReadEnvironment("LOCALAPPDATA", ".")) / "MastersOfAlbionReforged";
}

/// The settings file of the mods. MOA_MODS_SETTINGS gives the path, else it is in the settings folder.
[[nodiscard]] std::filesystem::path ModSettingsPath()
{
	if (const auto path = ReadEnvironment("MOA_MODS_SETTINGS", ""); !path.empty())
	{
		return path;
	}
	return SettingsFolder() / "mods.json";
}

/// The folder of the log files. MOA_LOADER_LOG_FOLDER gives the path, else it is `logs` in the settings folder.
[[nodiscard]] std::filesystem::path LogFolder()
{
	if (const auto path = ReadEnvironment("MOA_LOADER_LOG_FOLDER", ""); !path.empty())
	{
		return path;
	}
	return SettingsFolder() / "logs";
}

/// The folder of the mod zips and the mod DLLs: `mods` in the game folder. The loader only reads it.
[[nodiscard]] std::filesystem::path ModsFolder()
{
	return GameFolder() / "mods";
}

/// The folder of the map overrides.
[[nodiscard]] std::filesystem::path MapsFolder()
{
	return moa::mapoverride::MapsFolderIn(std::filesystem::path(ReadEnvironment("LOCALAPPDATA", ".")));
}

/// The marker file of the start, next to mods.json, never in a game folder.
[[nodiscard]] std::filesystem::path StartupMarkerPath()
{
	return SettingsFolder() / "startup-marker.json";
}

/// The folders that the loader must not write to: the game folders.
[[nodiscard]] std::vector<std::filesystem::path> GameFolders()
{
	return {GameFolder(), std::filesystem::path(k_OriginalGameFolder), std::filesystem::path(k_SteamGameFolder)};
}

[[nodiscard]] bool IsInGameFolder(const std::filesystem::path& path)
{
	std::error_code error;
	const auto absolute = std::filesystem::absolute(path, error);
	return error || std::ranges::any_of(GameFolders(), [&](const auto& game) { return IsInsideFolder(absolute, game); });
}

void StartLog(const std::filesystem::path& folder)
{
	std::error_code error;
	std::filesystem::create_directories(folder, error);
	const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
	const auto path = folder / std::format("mod-loader-{:%Y%m%d-%H%M%S}-{}.log", now, GetCurrentProcessId());
	auto logger = spdlog::basic_logger_mt("mod_loader", path.string());
	logger->flush_on(spdlog::level::info);
	spdlog::set_default_logger(std::move(logger));
}

/// Wait until IL2CPP has loaded its assemblies. The corlib image exists only after that.
[[nodiscard]] bool WaitForIl2Cpp()
{
	const auto deadline = std::chrono::steady_clock::now() + k_StartTimeout;
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (const auto module = GetModuleHandleW(L"GameAssembly.dll"); module != nullptr)
		{
			using GetCorlib = const void* (*)();
			const auto getCorlib = reinterpret_cast<GetCorlib>(GetProcAddress(module, "il2cpp_get_corlib"));
			if (getCorlib != nullptr && getCorlib() != nullptr)
			{
				std::this_thread::sleep_for(k_SettleTime);
				return true;
			}
		}
		std::this_thread::sleep_for(k_PollInterval);
	}
	return false;
}

/// A place for a text that a mod gives.
struct TextBox
{
	std::string text;

	[[nodiscard]] MoaText Target()
	{
		return MoaText {.context = this, .assign = [](void* context, const char* utf8, size_t size) {
			                if (utf8 != nullptr)
			                {
				                static_cast<TextBox*>(context)->text.assign(utf8, size);
			                }
		                }};
	}
};

/// The value of a field as JSON, in the form of mods.json.
[[nodiscard]] nlohmann::json FieldJson(const FieldEntry& field, const MoaFieldValue& value)
{
	switch (field.kind)
	{
	case MOA_FIELD_TOGGLE:
		return value.toggle != 0;
	case MOA_FIELD_NUMBER:
		return value.number;
	case MOA_FIELD_CHOICE:
		return value.choice < field.choices.size() ? nlohmann::json(field.choices[value.choice]) : nlohmann::json();
	}
	return {};
}

/// The state of the loader. It lives until the process ends.
struct LoaderState;

/// A field of the loader in the registry: the state and the field of the table.
struct LoaderField
{
	LoaderState* state = nullptr;
	const settings::Field<LoaderSettings>* field = nullptr;
};

/// A query of the loader itself: the state and the function of the query.
struct LoaderQuery
{
	LoaderState* state = nullptr;
	std::function<nlohmann::json(LoaderState& state, const nlohmann::json& params)> run;
};

struct LoaderState final: HostServices
{
	Il2CppApi api;
	std::filesystem::path logFolder;
	std::string buildGuid;
	/// The settings that the loader uses now. Only the main thread changes them after the start, with the lock held.
	LoaderSettings settings;
	mutable std::mutex settingsMutex;
	/// The settings that the user asked for last (the file, a hotkey or the menu bar). Main thread only.
	LoaderSettings requested;
	/// The text of mods.json that the loader read last, for the objects of the mods.
	nlohmann::json file = nlohmann::json::object();
	/// The settings file existed at the start and `liveReload` is true: the loader watches the file and reads the
	/// hotkeys.
	bool live = false;
	std::optional<Managed> managed;
	ModRegistry registry;
	std::unique_ptr<ModDllLoader> dlls;
	std::vector<std::unique_ptr<LoaderField>> loaderFields;
	std::vector<std::unique_ptr<LoaderQuery>> loaderQueries;
	/// The thread of the frame hook: the main thread of the game. Zero before the first frame.
	std::atomic<DWORD> mainThread = 0;
	/// One hook on each managed method that has callbacks.
	std::map<MethodKey, std::unique_ptr<MethodHook>> hooks;
	/// The hook on `GameManager.OnApplicationQuit`: the mods stop when the game quits.
	std::unique_ptr<MethodHook> quitHook;
	MainThreadTasks tasks;
	ModStatus objectsStatus {"objects"};
	ModStatus mapsStatus {"maps"};
	std::unique_ptr<objects::ObjectsMod> objects;
	std::unique_ptr<maps::MapsMod> maps;
	mods::ModCatalogue catalogue;
	std::optional<HotkeyInput> hotkeyInput;
	std::vector<HotkeyBinding> hotkeyBindings;
	std::unique_ptr<GameMessages> messages;
	nlohmann::json lastReload;
	std::jthread watcher;
	std::unique_ptr<Overlay> overlay;
	std::optional<GameMenuProbe> menuProbe;
	bool overlayPin = false;
	uint64_t overlayFrames = 0;
	StartupSequence startup;
	StartupMarker marker;
	bool markerWritten = false;
	std::vector<std::string> featuresOff;

	ModRegistry& Registry() override { return registry; }

	void Log(OwnerId owner, MoaLogLevel level, std::string_view text) override
	{
		const auto who = owner == k_LoaderOwner ? std::string("loader") : registry.OwnerName(owner);
		switch (level)
		{
		case MOA_LOG_DEBUG:
			spdlog::debug("[{}] {}", who, text);
			break;
		case MOA_LOG_INFO:
			spdlog::info("[{}] {}", who, text);
			break;
		case MOA_LOG_WARN:
			spdlog::warn("[{}] {}", who, text);
			break;
		default:
			spdlog::error("[{}] {}", who, text);
			break;
		}
	}

	moa::Result<void> PostTask(OwnerId owner, MoaTaskFn task, void* user) override;
	moa::Result<void> SaveSettings(OwnerId owner, std::string_view patch) override;
	void ShowMessage(OwnerId owner, std::string_view text, double seconds) override;
	moa::Result<std::string> CallQuery(const QueryEntry& query, const std::string& params) override;
};

/// The state, for the removal of the hooks when the DLL unloads. Only `Start` writes it, and `DllMain` reads it.
std::atomic<LoaderState*> s_state = nullptr;

/// Stop the calls to a mod after an error in its code.
void FailOwner(LoaderState& state, OwnerId owner, const std::string& reason)
{
	if (owner == k_LoaderOwner || state.registry.Failed(owner))
	{
		return;
	}
	state.registry.SetFailed(owner, reason);
	if (state.dlls)
	{
		state.dlls->MarkFailed(owner, reason);
	}
	spdlog::error("[{}] {}; the loader calls the mod no more, the game continues", state.registry.OwnerName(owner), reason);
}

/// Call a function of a mod with a guard. An access violation stops all calls to that mod. Return false if the call
/// did not complete.
template <typename Work>
bool CallMod(LoaderState& state, OwnerId owner, const char* what, Work& work)
{
	if (state.registry.Failed(owner))
	{
		return false;
	}
	const auto thunk = [](void* context) { (*static_cast<Work*>(context))(); };
	if (GuardedCall(thunk, &work, &FilterAccessViolation))
	{
		return true;
	}
	FailOwner(state, owner, std::format("an access violation in {}", what));
	return false;
}

/// Write a change to mods.json. The loader changes only the fields of the change; the other fields of the file stay.
moa::Result<void> SaveSettingsFile(const nlohmann::json& patch)
{
	const auto path = ModSettingsPath();
	auto text = PatchSettingsText(ReadTextFile(path).value_or(""), patch);
	if (!text)
	{
		return text.GetError();
	}
	if (auto checked = ParseLoaderSettings(*text); !checked)
	{
		return moa::MakeError("the changed settings are not correct: {}", checked.GetError().message);
	}
	std::error_code error;
	std::filesystem::create_directories(path.parent_path(), error);
	return WriteTextFileAtomically(path, *text);
}

void SaveOrWarn(const nlohmann::json& patch)
{
	if (auto saved = SaveSettingsFile(patch); !saved)
	{
		spdlog::warn("mod settings: {}; the change is only for this game", saved.GetError().message);
	}
}

moa::Result<void> LoaderState::PostTask(OwnerId owner, MoaTaskFn task, void* user)
{
	(void)tasks.Post(MainThreadTasks::Task([this, owner, task, user] {
		auto work = [&] { task(user); };
		(void)CallMod(*this, owner, "a task", work);
		return nlohmann::json();
	}));
	return {};
}

moa::Result<void> LoaderState::SaveSettings(OwnerId owner, std::string_view patch)
{
	const auto object = nlohmann::json::parse(patch, nullptr, false);
	if (object.is_discarded() || !object.is_object())
	{
		return moa::MakeError("the change is not a JSON object");
	}
	for (const auto& [key, value] : object.items())
	{
		const auto owned = std::ranges::any_of(
		    registry.Settings(), [&](const SettingsEntry& entry) { return entry.owner == owner && entry.section == key; });
		if (!owned)
		{
			return moa::MakeError("the mod does not own the object '{}' of mods.json", key);
		}
	}
	return SaveSettingsFile(object);
}

void LoaderState::ShowMessage(OwnerId owner, std::string_view text, double seconds)
{
	auto copy = std::string(text);
	(void)tasks.Post(MainThreadTasks::Task([this, owner, copy = std::move(copy), seconds] {
		const auto shown = std::max(seconds, 0.5);
		if (messages && settings.hotkeys.messages)
		{
			messages->Show(copy, shown);
		}
		else
		{
			spdlog::info("[{}] message: {}", owner == k_LoaderOwner ? std::string("loader") : registry.OwnerName(owner), copy);
		}
		return nlohmann::json();
	}));
}

moa::Result<std::string> LoaderState::CallQuery(const QueryEntry& query, const std::string& params)
{
	// The queries read and change Unity objects, so they run only on the main thread.
	if (GetCurrentThreadId() != mainThread.load())
	{
		return moa::MakeError("the query '{}' runs only on the main thread of the game", query.name);
	}
	if (registry.Failed(query.owner))
	{
		return moa::MakeError("the loader calls {} no more: {}", registry.OwnerName(query.owner),
		                      registry.FailReason(query.owner));
	}
	moa::Result<std::string> answer = moa::MakeError("the query '{}' did not complete", query.name);
	auto work = [&] { answer = RunQueryFunction(query, params); };
	if (!CallMod(*this, query.owner, "a query", work))
	{
		return moa::MakeError("an access violation in the query '{}'", query.name);
	}
	return answer;
}

/// Tell the user about a change: a message of the game and a line in the log. Main thread.
void Notify(LoaderState& state, const std::string& text, double seconds)
{
	if (state.messages && state.settings.hotkeys.messages)
	{
		state.messages->Show(text, seconds);
		return;
	}
	spdlog::info("mod message: {}", text);
}

/// Read mods.json. If the file does not exist, the loader uses the defaults and the mods get empty objects. If the
/// file is not correct, the log tells the error and the loader uses the defaults.
void LoadSettingsFile(LoaderState& state)
{
	const auto path = ModSettingsPath();
	const auto text = ReadTextFile(path);
	if (!text)
	{
		spdlog::info("mod settings: no file at {}; the defaults apply", path.string());
		return;
	}
	auto settings = ParseLoaderSettings(*text);
	if (!settings)
	{
		spdlog::error("mod settings: {}: {}; the defaults apply", path.string(), settings.GetError().message);
		return;
	}
	state.settings = *settings;
	state.requested = *settings;
	state.file = nlohmann::json::parse(*text);
	spdlog::info("mod settings: read {}", path.string());
}

/// Give each mod its objects of mods.json. A mod that refuses its objects keeps the settings that it has.
nlohmann::json DeliverSettings(LoaderState& state)
{
	auto refused = nlohmann::json::array();
	for (const auto& entry : state.registry.Settings())
	{
		const auto section = state.file.contains(entry.section) ? state.file.at(entry.section) : nlohmann::json::object();
		const auto text = section.dump();
		TextBox error;
		uint32_t accepted = 0;
		auto target = error.Target();
		auto work = [&] { accepted = entry.load(entry.user, text.c_str(), &target); };
		if (!CallMod(state, entry.owner, "the settings function", work))
		{
			continue;
		}
		if (accepted == 0)
		{
			spdlog::error("[{}] the mod refuses the object '{}' of mods.json: {}; the mod keeps its settings",
			              state.registry.OwnerName(entry.owner), entry.section, error.text);
			refused.push_back({{"section", entry.section}, {"error", error.text}});
		}
	}
	return refused;
}

/// Use the hotkeys of the settings: the defaults of the actions, with the hotkeys of mods.json in their place.
void ApplyHotkeys(LoaderState& state)
{
	std::vector<std::string> errors;
	state.hotkeyBindings = state.registry.ResolveHotkeys(state.settings.hotkeys.keys, errors);
	for (const auto& error : errors)
	{
		spdlog::warn("hotkeys: {}", error);
	}
	if (!state.hotkeyInput)
	{
		return;
	}
	std::vector<HotkeyInput::NamedHotkey> named;
	std::string keys;
	for (const auto& binding : state.hotkeyBindings)
	{
		named.push_back({.name = binding.name, .hotkey = binding.hotkey});
		keys += std::format(" {}={}", binding.name, FormatHotkey(binding.hotkey));
	}
	state.hotkeyInput->SetHotkeys(state.settings.hotkeys.enabled, named);
	spdlog::info("hotkeys: {}{}", state.settings.hotkeys.enabled ? "on" : "off", keys);
}

/// Use new settings of the loader in the running game. Main thread, between two frames.
nlohmann::json ApplyLoaderSettings(LoaderState& state, const LoaderSettings& next)
{
	const auto change = CompareLoaderSettings(state.settings, next);
	for (const auto& name : change.restartOnly)
	{
		spdlog::warn("mod settings: '{}' changed; the change works after a restart of the game", name);
	}
	state.requested = next;
	{
		const std::scoped_lock lock(state.settingsMutex);
		state.settings = LiveLoaderSettings(state.settings, next);
	}
	if (change.hotkeys)
	{
		ApplyHotkeys(state);
	}
	return change.Describe();
}

/// Use the settings that the loader read from the file. A file that is not correct changes nothing: the loader and
/// the mods keep the settings that they have, and the log and a message tell the error.
nlohmann::json ApplyReload(LoaderState& state, const char* source)
{
	const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
	nlohmann::json result {{"time", std::format("{:%Y-%m-%d %H:%M:%S}", now)}, {"source", source}};
	const auto seconds = state.settings.hotkeys.messageSeconds;
	const auto path = ModSettingsPath();
	const auto text = ReadTextFile(path);
	auto settings = text ? ParseLoaderSettings(*text)
	                     : moa::Result<LoaderSettings>(moa::MakeError("the file {} cannot be read", path.string()));
	if (!settings)
	{
		spdlog::error("mod settings: {}: {}; the mods keep their current settings", path.string(), settings.GetError().message);
		Notify(state, "Mod settings: the file has an error. The mods keep their settings.", seconds);
		result["applied"] = false;
		result["error"] = settings.GetError().message;
	}
	else
	{
		state.file = nlohmann::json::parse(*text);
		result["loader"] = ApplyLoaderSettings(state, *settings);
		result["refused"] = DeliverSettings(state);
		result["applied"] = true;
		spdlog::info("mod settings: read {} ({}): {}", path.string(), source, result.dump());
		if (std::string_view(source) != "file")
		{
			Notify(state, "Mod settings: reloaded", state.settings.hotkeys.messageSeconds);
		}
	}
	state.lastReload = result;
	return result;
}

/// Write the marker of this start with its current stage. If the write fails, the log tells it and the start
/// continues with no protection for the next start.
void WriteStartupMarker(LoaderState& state, std::string stage)
{
	state.marker.stage = std::move(stage);
	const auto path = StartupMarkerPath();
	std::error_code error;
	std::filesystem::create_directories(path.parent_path(), error);
	if (auto written = WriteTextFileAtomically(path, FormatStartupMarker(state.marker)); !written)
	{
		spdlog::warn("start-up: the loader could not write the marker {}: {}", path.string(), written.GetError().message);
		return;
	}
	state.markerWritten = true;
}

/// Remove the marker of this start: the start completed.
void ClearStartupMarker(LoaderState& state, const std::string& reason)
{
	if (!state.markerWritten)
	{
		return;
	}
	state.markerWritten = false;
	std::error_code error;
	std::filesystem::remove(StartupMarkerPath(), error);
	if (error)
	{
		spdlog::warn("start-up: the loader could not remove the marker {}: {}", StartupMarkerPath().string(), error.message());
		return;
	}
	spdlog::info("start-up: {}; the loader removed the marker {}", reason, StartupMarkerPath().string());
}

/// The name of a mod DLL in the list of the features of the start-up guard.
[[nodiscard]] std::string DllFeature(const std::string& file)
{
	return "dll:" + file;
}

/// Read the marker of the last start, decide which features this start uses, and write the marker of this start.
/// After a start that did not complete, each feature that it used stays off, also in mods.json, so that the game
/// does not stop at each start: the menu bar, and each mod DLL. The user turns the feature on again.
void BeginStartupGuard(LoaderState& state, const std::vector<std::filesystem::path>& dlls)
{
	std::vector<std::string> requested;
	if (state.live && state.settings.overlay.enabled)
	{
		requested.emplace_back(k_FeatureOverlay);
	}
	for (const auto& dll : dlls)
	{
		const auto file = Utf8(dll.filename());
		if (std::ranges::find(state.settings.modDlls.disabled, file) == state.settings.modDlls.disabled.end())
		{
			requested.push_back(DllFeature(file));
		}
	}
	const auto decision = DecideStartup(ReadTextFile(StartupMarkerPath()), requested);
	if (decision.lastStartFailed)
	{
		spdlog::error("start-up: !!! THE LAST START OF THE GAME DID NOT COMPLETE !!! The marker {} is there: the game "
		              "stopped, or the user stopped it, before it was stable.",
		              StartupMarkerPath().string());
		if (const auto& last = decision.lastStart)
		{
			spdlog::error("start-up: the last start: process {}, loader {}, build {}, started {}, last stage '{}', "
			              "features [{}]",
			              last->processId, last->version, last->buildGuid, last->startedAt, last->stage,
			              nlohmann::json(last->features).dump());
		}
		else
		{
			spdlog::error("start-up: the marker of the last start is not correct: {}", decision.markerError);
		}
	}
	for (const auto& feature : decision.featuresOff)
	{
		if (feature == k_FeatureOverlay)
		{
			state.settings.overlay.enabled = false;
			state.requested.overlay.enabled = false;
			SaveOrWarn({{"overlay", {{"enabled", false}}}});
			spdlog::error("start-up: !!! THE MENU BAR IS OFF !!! The last start used it and did not complete. The loader "
			              "wrote overlay.enabled false to mods.json. Set it to true again to try the menu bar.");
		}
		else if (feature.starts_with("dll:"))
		{
			const auto file = feature.substr(4);
			state.settings.modDlls.disabled.push_back(file);
			state.requested.modDlls.disabled.push_back(file);
			SaveOrWarn({{"modDlls", {{"disabled", state.requested.modDlls.disabled}}}});
			spdlog::error("start-up: !!! THE MOD DLL {} IS OFF !!! The last start used it and did not complete. The "
			              "loader added it to modDlls.disabled in mods.json. Remove it from that list to try it again.",
			              file);
		}
		state.featuresOff.push_back(feature);
	}
	const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
	state.marker = StartupMarker {.processId = static_cast<uint32_t>(GetCurrentProcessId()),
	                              .version = MoaModLoaderVersion(),
	                              .buildGuid = state.buildGuid,
	                              .startedAt = std::format("{:%Y-%m-%d %H:%M:%S}", now),
	                              .stage = {},
	                              .features = decision.featuresOn};
	WriteStartupMarker(state, "start");
	if (state.markerWritten)
	{
		spdlog::info("start-up: the marker {} is there until the game is stable (features [{}])", StartupMarkerPath().string(),
		             nlohmann::json(decision.featuresOn).dump());
	}
}

/// Do an action of the loader or of a mod. Main thread.
void RunAction(LoaderState& state, size_t id)
{
	const auto* action = state.registry.FindAction(id);
	if (action == nullptr)
	{
		return;
	}
	spdlog::info("action: {} ({})", action->hotkey.empty() ? action->label : action->hotkey,
	             state.registry.OwnerName(action->owner));
	auto work = [action] { action->run(action->user); };
	(void)CallMod(state, action->owner, "an action", work);
}

/// Read the hotkeys and remove an old message. Main thread, one time in each frame.
void ReadControls(LoaderState& state)
{
	if (state.hotkeyInput)
	{
		for (const auto index : state.hotkeyInput->Poll())
		{
			if (index < state.hotkeyBindings.size())
			{
				RunAction(state, state.hotkeyBindings[index].actionId);
			}
		}
	}
	if (state.messages)
	{
		state.messages->Tick();
	}
}

/// A short text with the state of each mod DLL and the toggles named "enabled", for a message on the screen.
[[nodiscard]] std::string SettingsSummary(LoaderState& state)
{
	std::string text;
	if (state.dlls)
	{
		for (const auto& mod : state.dlls->Mods())
		{
			text += std::format("{} {}: {}\n", mod->name.empty() ? mod->file : mod->name, mod->version,
			                    ModDllStateName(mod->state));
		}
	}
	for (const auto& field : state.registry.Fields())
	{
		if (field.owner == k_LoaderOwner || field.kind != MOA_FIELD_TOGGLE || field.key != "enabled")
		{
			continue;
		}
		MoaFieldState value {};
		auto work = [&] { field.get(field.user, &value); };
		if (CallMod(state, field.owner, "a field", work))
		{
			text += std::format("{}: {}\n", field.label, value.value.toggle != 0 ? "on" : "off");
		}
	}
	text += std::format("Hotkeys: {}, menu bar: {}", state.settings.hotkeys.enabled ? "on" : "off",
	                    state.overlay ? "ready" : "off");
	return text;
}

/// Register the fields and the actions of the loader itself (owner 0).
void RegisterLoaderItems(LoaderState& state)
{
	state.registry.SetOwnerName(k_LoaderOwner, "Mods");
	for (const auto& field : LoaderFields())
	{
		auto binding = std::make_unique<LoaderField>(LoaderField {.state = &state, .field = &field});
		const auto section = std::string(field.section);
		const auto key = std::string(field.key);
		const auto group = std::string(field.group);
		const auto label = std::string(field.label);
		const auto help = std::string(field.help);
		const auto format = std::string(field.format);
		const MoaFieldDesc desc {.menu = nullptr,
		                         .group = group.c_str(),
		                         .section = section.c_str(),
		                         .key = key.c_str(),
		                         .label = label.c_str(),
		                         .help = help.c_str(),
		                         .kind = field.kind == settings::FieldKind::Number ? MOA_FIELD_NUMBER : MOA_FIELD_TOGGLE,
		                         .min = field.min,
		                         .max = field.max,
		                         .step = field.step,
		                         .format = format.c_str(),
		                         .logarithmic = field.logarithmic ? 1u : 0u,
		                         .restartOnly = field.restartOnly ? 1u : 0u,
		                         .choices = nullptr,
		                         .choiceCount = 0};
		const auto get = [](void* user, MoaFieldState* out) {
			const auto& b = *static_cast<LoaderField*>(user);
			const auto& requested = b.state->requested;
			const auto value = settings::ValueOf(*b.field, requested);
			const auto live = settings::ValueOf(*b.field, b.state->settings);
			*out = MoaFieldState {.value = {}, .min = b.field->min, .max = b.field->max, .restartPending = 0};
			if (const auto* toggle = std::get_if<bool>(&value))
			{
				out->value.toggle = *toggle ? 1u : 0u;
			}
			else if (const auto* number = std::get_if<double>(&value))
			{
				out->value.number = *number;
			}
			out->restartPending = b.field->restartOnly && value != live ? 1u : 0u;
		};
		const auto set = [](void* user, const MoaFieldValue* value, uint32_t /*save*/, MoaText* message) -> uint32_t {
			auto& b = *static_cast<LoaderField*>(user);
			auto next = b.state->requested;
			const settings::FieldValue input = b.field->kind == settings::FieldKind::Number
			                                       ? settings::FieldValue {value->number}
			                                       : settings::FieldValue {value->toggle != 0};
			auto edit = EditLoaderField(next, *b.field, input);
			const auto& text = edit ? edit->message : edit.GetError().message;
			message->assign(message->context, text.data(), text.size());
			if (!edit)
			{
				return 0;
			}
			(void)ApplyLoaderSettings(*b.state, next);
			return 1;
		};
		if (auto added = state.registry.AddField(k_LoaderOwner, desc, get, set, binding.get()); !added)
		{
			spdlog::error("loader: the field {} is refused: {}", key, added.GetError().message);
		}
		state.loaderFields.push_back(std::move(binding));
	}
	struct LoaderAction
	{
		const char* hotkey;
		const char* defaultHotkey;
		MoaTaskFn run;
	};
	const std::array actions {
	    LoaderAction {"reloadSettings", "Ctrl+Shift+F10",
	                  [](void* user) { (void)ApplyReload(*static_cast<LoaderState*>(user), "hotkey"); }},
	    LoaderAction {"showSettings", "Ctrl+Shift+F11",
	                  [](void* user) {
		                  auto& s = *static_cast<LoaderState*>(user);
		                  Notify(s, SettingsSummary(s), std::max(s.settings.hotkeys.messageSeconds, k_SummarySeconds));
	                  }},
	    LoaderAction {"toggleOverlay", "Ctrl+Shift+F4",
	                  [](void* user) {
		                  auto& s = *static_cast<LoaderState*>(user);
		                  if (!s.overlay)
		                  {
			                  Notify(s, "Menu bar: not ready (see the log)", s.settings.hotkeys.messageSeconds);
			                  return;
		                  }
		                  s.overlayPin = true;
	                  }},
	};
	for (const auto& action : actions)
	{
		const MoaActionDesc desc {.menu = nullptr,
		                          .group = "Hotkeys",
		                          .label = nullptr,
		                          .help = nullptr,
		                          .hotkey = action.hotkey,
		                          .defaultHotkey = action.defaultHotkey};
		if (auto added = state.registry.AddAction(k_LoaderOwner, desc, action.run, nullptr, &state); !added)
		{
			spdlog::error("loader: the action {} is refused: {}", action.hotkey, added.GetError().message);
		}
	}
}

/// Find the names that the hotkeys and the messages need. If a name is missing, the hotkeys are off and the log tells
/// why; the live reload of the file still works.
void InstallControls(LoaderState& state)
{
	state.messages = std::make_unique<GameMessages>(*state.managed);
	if (auto input = HotkeyInput::Bind(*state.managed); input)
	{
		state.hotkeyInput = std::move(*input);
	}
	else
	{
		spdlog::error("hotkeys: {}; the hotkeys are off", input.GetError().message);
	}
	ApplyHotkeys(state);
}

/// Start the thread that watches the settings file. It finds a changed file on its own thread, and the main thread
/// reads and uses it between two frames.
void StartSettingsWatcher(LoaderState& state)
{
	state.watcher = std::jthread([&state](const std::stop_token& stop) {
		FileChangeMonitor monitor(ModSettingsPath());
		spdlog::info("mod settings: the loader watches {} for changes", monitor.Path().string());
		while (!stop.stop_requested())
		{
			std::this_thread::sleep_for(k_SettingsWatchInterval);
			// No exception may stop the process from this thread.
			try
			{
				if (monitor.Changed())
				{
					(void)state.tasks.Post(MainThreadTasks::Task([&state] { return ApplyReload(state, "file"); }));
				}
			}
			catch (const std::exception& error)
			{
				spdlog::error("mod settings: the watcher of the file stops: {}", error.what());
				return;
			}
		}
	});
}

/// Read and check each zip in the folder of the mods, and decide which mods load and which map the game gets. A zip
/// that is not correct stays out; the log and the Mod list tell why. The loader only reads the folder.
void LoadCatalogue(LoaderState& state)
{
	using mods::CatalogueMod;
	const auto folder = ModsFolder();
	const moa::modzip::ZipLimits limits;
	std::vector<CatalogueMod> found;
	std::error_code listError;
	for (const auto& entry : std::filesystem::directory_iterator(folder, listError))
	{
		const auto path = entry.path();
		auto extension = path.extension().string();
		std::ranges::transform(extension, extension.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });
		if (extension != ".zip")
		{
			continue;
		}
		const auto file = Utf8(path.filename());
		std::error_code error;
		if (entry.is_symlink(error) || !entry.is_regular_file(error))
		{
			found.push_back(CatalogueMod {.file = file, .error = "the zip is not a plain file"});
			continue;
		}
		const auto size = entry.file_size(error);
		if (error || size > limits.maxArchiveBytes)
		{
			found.push_back(CatalogueMod {
			    .file = file,
			    .error = error ? std::format("the zip cannot be read: {}", error.message())
			                   : std::format("the zip has {} bytes; the limit is {}", size, limits.maxArchiveBytes)});
			continue;
		}
		std::vector<std::byte> bytes(static_cast<size_t>(size));
		std::ifstream stream(path, std::ios::binary);
		if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
		{
			found.push_back(CatalogueMod {.file = file, .error = "the zip cannot be read"});
			continue;
		}
		found.push_back(mods::OpenModZip(file, std::move(bytes), state.buildGuid, limits));
	}
	state.catalogue = mods::BuildCatalogue(std::move(found), state.settings);
	spdlog::info("mods: {} zips in {}", state.catalogue.mods.size(), folder.string());
	for (const auto& mod : state.catalogue.mods)
	{
		const auto label = mod.info ? std::format("{} {} ({})", mod.info->id, mod.info->version, mod.file) : mod.file;
		if (mod.state == mods::ModState::Refused)
		{
			spdlog::error("mods:   {} is refused: {}", label, mod.error);
		}
		else
		{
			spdlog::info("mods:   {} is {}{}", label, mods::ModStateName(mod.state),
			             mod.mapActive ? ", and its map loads" : "");
		}
	}
	if (!state.catalogue.mapNote.empty())
	{
		spdlog::warn("mods: {}", state.catalogue.mapNote);
	}
}

/// The mod zips for the Mod list: the catalogue, and for each mod that loads the status of its map and its packs.
/// Main thread.
[[nodiscard]] nlohmann::json DescribeModList(const LoaderState& state)
{
	auto list = mods::DescribeCatalogue(state.catalogue);
	const auto objects = state.objects ? state.objects->PackStatus() : nlohmann::json::object();
	const auto loaded = objects.value("packs", nlohmann::json::array());
	const auto packErrors = objects.value("packErrors", nlohmann::json::object());
	for (auto& mod : list.at("mods"))
	{
		if (mod.value("state", "") != "on")
		{
			continue;
		}
		auto notes = nlohmann::json::array();
		const auto note = [&notes](std::string text, bool error) {
			notes.push_back({{"text", std::move(text)}, {"error", error}});
		};
		if (mod.value("mapActive", false))
		{
			if (state.maps)
			{
				size_t applied = 0;
				size_t refused = 0;
				for (const auto& item : state.maps->State().value("items", nlohmann::json::array()))
				{
					applied += item.value("state", "") == "applied" ? 1 : 0;
					refused += item.value("state", "") == "refused" ? 1 : 0;
				}
				note(std::format("map: {} items applied, {} refused", applied, refused), refused > 0);
			}
			else
			{
				note(std::format("map: not used: {}", state.mapsStatus.Describe().value("lastError", "")), true);
			}
		}
		for (const auto& folder : mod.value("packs", nlohmann::json::array()))
		{
			const auto name = folder.get<std::string>();
			const auto key = std::format("mods/{}/{}", mod.value("id", ""), name);
			const bool used = std::ranges::find(loaded, nlohmann::json(key)) != loaded.end();
			note(std::format("pack {}: {}", name, used ? "loaded" : "not used"), !used);
			for (const auto& error : packErrors.value(key, nlohmann::json::array()))
			{
				note(std::format("pack {}: {}", name, error.get<std::string>()), true);
			}
		}
		mod["notes"] = std::move(notes);
	}
	return list;
}

/// The mod DLLs for the Mod list and the status.
[[nodiscard]] nlohmann::json DescribeDlls(const LoaderState& state)
{
	auto list = nlohmann::json::array();
	if (!state.dlls)
	{
		return list;
	}
	for (const auto& mod : state.dlls->Mods())
	{
		list.push_back({{"file", mod->file},
		                {"id", mod->id},
		                {"name", mod->name},
		                {"version", mod->version},
		                {"state", ModDllStateName(mod->state)},
		                {"error", mod->error}});
	}
	return list;
}

/// Read one item of the registry for the menu bar. Main thread.
[[nodiscard]] MenuItem ReadField(LoaderState& state, const FieldEntry& field)
{
	MenuItem item {.kind = MenuItem::Kind::Field,
	               .id = field.id,
	               .group = field.group,
	               .label = field.label,
	               .help = field.help,
	               .path = field.Path(),
	               .fieldKind = field.kind,
	               .format = field.format,
	               .logarithmic = field.logarithmic,
	               .restartOnly = field.restartOnly,
	               .choices = field.choices};
	item.state = MoaFieldState {.value = {}, .min = field.min, .max = field.max, .restartPending = 0};
	auto work = [&] { field.get(field.user, &item.state); };
	(void)CallMod(state, field.owner, "a field", work);
	return item;
}

/// The menus of the mods and the items of the loader, for the menu bar. Main thread.
void ReadMenus(LoaderState& state, OverlaySnapshot& snapshot)
{
	struct Ordered
	{
		OwnerId owner;
		std::string menu;
		MenuItem item;
	};
	std::vector<Ordered> items;
	for (const auto& field : state.registry.Fields())
	{
		if (!state.registry.Failed(field.owner))
		{
			items.push_back({field.owner, field.menu, ReadField(state, field)});
		}
	}
	std::map<size_t, std::string> hotkeys;
	for (const auto& binding : state.hotkeyBindings)
	{
		hotkeys[binding.actionId] = FormatHotkey(binding.hotkey);
	}
	for (const auto& action : state.registry.Actions())
	{
		if (action.label.empty() || state.registry.Failed(action.owner))
		{
			continue;
		}
		MenuItem item {.kind = MenuItem::Kind::Action,
		               .id = action.id,
		               .group = action.group,
		               .label = action.label,
		               .help = action.help,
		               .hotkey = hotkeys.contains(action.id) ? hotkeys.at(action.id) : std::string()};
		if (action.enabled != nullptr)
		{
			uint32_t enabled = 0;
			auto work = [&] { enabled = action.enabled(action.user); };
			item.enabled = CallMod(state, action.owner, "an action", work) && enabled != 0;
		}
		items.push_back({action.owner, action.menu, std::move(item)});
	}
	for (const auto& readout : state.registry.Readouts())
	{
		if (state.registry.Failed(readout.owner))
		{
			continue;
		}
		TextBox text;
		auto target = text.Target();
		auto work = [&] { readout.read(readout.user, &target); };
		(void)CallMod(state, readout.owner, "a readout", work);
		items.push_back(
		    {readout.owner, readout.menu,
		     MenuItem {.kind = MenuItem::Kind::Readout, .id = readout.id, .group = readout.group, .text = text.text}});
	}
	// The order of the registrations in each menu, and the order of the owners for the menus.
	std::ranges::stable_sort(
	    items, [](const Ordered& a, const Ordered& b) { return std::tie(a.owner, a.item.id) < std::tie(b.owner, b.item.id); });
	for (auto& [owner, menuName, item] : items)
	{
		if (owner == k_LoaderOwner && menuName.empty())
		{
			snapshot.loaderItems.push_back(std::move(item));
			continue;
		}
		const auto name = menuName.empty() ? state.registry.OwnerName(owner) : menuName;
		auto menu = std::ranges::find(snapshot.menus, name, &Menu::name);
		if (menu == snapshot.menus.end())
		{
			snapshot.menus.push_back(Menu {.name = name, .items = {}});
			menu = std::prev(snapshot.menus.end());
		}
		menu->items.push_back(std::move(item));
	}
	for (const auto& binding : state.hotkeyBindings)
	{
		const auto* action = state.registry.FindAction(binding.actionId);
		snapshot.hotkeys.push_back({.name = binding.name,
		                            .hotkey = FormatHotkey(binding.hotkey),
		                            .owner = action != nullptr ? state.registry.OwnerName(action->owner) : std::string()});
	}
}

/// The status of each mod DLL (its status reader) and of the parts of the loader. Main thread. The menu bar shows the
/// name of the mod. With `keepNames`, a status keeps its own name and gets the name of the mod as "mod".
[[nodiscard]] nlohmann::json ReadStatuses(LoaderState& state, bool keepNames = false)
{
	auto list = nlohmann::json::array({state.objectsStatus.Describe(), state.mapsStatus.Describe()});
	for (const auto& entry : state.registry.Statuses())
	{
		TextBox text;
		auto target = text.Target();
		auto work = [&] { entry.read(entry.user, &target); };
		if (!CallMod(state, entry.owner, "the status reader", work))
		{
			continue;
		}
		auto status = nlohmann::json::parse(text.text, nullptr, false);
		if (status.is_discarded() || !status.is_object())
		{
			status = {{"error", "the status is not a JSON object"}};
		}
		if (keepNames && status.contains("name"))
		{
			status["mod"] = state.registry.OwnerName(entry.owner);
		}
		else
		{
			status["name"] = state.registry.OwnerName(entry.owner);
		}
		list.push_back(std::move(status));
	}
	if (state.dlls)
	{
		for (const auto& mod : state.dlls->Mods())
		{
			if (mod->state == ModDllState::Failed)
			{
				list.push_back({{"name", mod->name.empty() ? mod->file : mod->name}, {"lastError", mod->error}});
			}
		}
	}
	return list;
}

/// The state of the mods for the menu bar. Main thread.
[[nodiscard]] OverlaySnapshot MakeOverlaySnapshot(LoaderState& state)
{
	OverlaySnapshot snapshot {
	    .settings = state.settings,
	    .requested = state.requested,
	    .mods = ReadStatuses(state),
	    .hooks = nlohmann::json::object(),
	    .lastReload = state.lastReload,
	    .settingsPath = ModSettingsPath().string(),
	    .buildGuid = state.buildGuid,
	    .version = MoaModLoaderVersion(),
	    .live = state.live,
	    .hotkeysReady = state.hotkeyInput.has_value(),
	    .maps = maps::ListMapOverrides(MapsFolder()),
	    .modList = DescribeModList(state),
	    .dllList = DescribeDlls(state),
	    .modsFolder = ModsFolder().string(),
	};
	for (const auto& [method, hook] : state.hooks)
	{
		snapshot.hooks[method.Text()] = hook->Describe();
	}
	ReadMenus(state, snapshot);
	return snapshot;
}

/// Write a field to mods.json after a change: the value that the owner of the field has now.
void SaveField(LoaderState& state, const FieldEntry& field)
{
	MoaFieldState current {};
	auto work = [&] { field.get(field.user, &current); };
	if (!CallMod(state, field.owner, "a field", work))
	{
		return;
	}
	nlohmann::json patch;
	if (field.section.empty())
	{
		patch[field.key] = FieldJson(field, current.value);
	}
	else
	{
		patch[field.section][field.key] = FieldJson(field, current.value);
	}
	SaveOrWarn(patch);
}

/// Do a change of the menu bar on the main thread, between two frames.
void ApplyOverlayCommand(LoaderState& state, const OverlayCommand& command)
{
	switch (command.kind)
	{
	case OverlayCommand::Kind::Reload:
		(void)ApplyReload(state, "menu bar");
		break;
	case OverlayCommand::Kind::Action:
		RunAction(state, command.id);
		break;
	case OverlayCommand::Kind::Mod:
	case OverlayCommand::Kind::Map:
	{
		auto next = state.requested;
		auto edit = command.kind == OverlayCommand::Kind::Mod ? mods::EditModEnabled(next, command.name, command.on)
		                                                      : mods::EditMapChoice(next, command.map);
		if (!edit)
		{
			spdlog::warn("menu bar: {}", edit.GetError().message);
			break;
		}
		(void)ApplyLoaderSettings(state, next);
		spdlog::info("menu bar: {}", edit->message);
		SaveOrWarn(edit->patch);
		break;
	}
	case OverlayCommand::Kind::ModDll:
	{
		auto next = state.requested;
		auto& disabled = next.modDlls.disabled;
		std::erase(disabled, command.name);
		if (!command.on)
		{
			disabled.push_back(command.name);
		}
		(void)ApplyLoaderSettings(state, next);
		spdlog::info("menu bar: mod DLL {}: {} after a restart of the game", command.name, command.on ? "on" : "off");
		SaveOrWarn({{"modDlls", {{"disabled", disabled}}}});
		break;
	}
	case OverlayCommand::Kind::Edit:
	{
		const auto* field = state.registry.FindField(command.id);
		if (field == nullptr)
		{
			break;
		}
		TextBox message;
		auto target = message.Target();
		uint32_t accepted = 0;
		auto work = [&] { accepted = field->set(field->user, &command.value, command.save ? 1u : 0u, &target); };
		if (!CallMod(state, field->owner, "a field", work))
		{
			break;
		}
		if (accepted == 0)
		{
			spdlog::warn("menu bar: {}: {}", field->Path(), message.text);
			break;
		}
		if (command.save)
		{
			spdlog::info("menu bar: {}", message.text);
			SaveField(state, *field);
		}
		break;
	}
	}
	if (state.overlay)
	{
		state.overlay->Publish(MakeOverlaySnapshot(state));
	}
}

/// Find a field of the loader or of a mod by its object of mods.json and its key.
[[nodiscard]] const FieldEntry* FindFieldByPath(const LoaderState& state, const std::string& section, const std::string& key)
{
	const auto& fields = state.registry.Fields();
	const auto found =
	    std::ranges::find_if(fields, [&](const FieldEntry& field) { return field.section == section && field.key == key; });
	return found != fields.end() ? &*found : nullptr;
}

/// The value of a JSON parameter for a field of the menu bar.
[[nodiscard]] MoaFieldValue FieldValueOf(const FieldEntry& field, const nlohmann::json& value)
{
	MoaFieldValue result {};
	switch (field.kind)
	{
	case MOA_FIELD_TOGGLE:
		if (!value.is_boolean())
		{
			throw std::runtime_error(std::format("'{}' needs true or false", field.Path()));
		}
		result.toggle = value.get<bool>() ? 1u : 0u;
		break;
	case MOA_FIELD_NUMBER:
		if (!value.is_number())
		{
			throw std::runtime_error(std::format("'{}' needs a number", field.Path()));
		}
		result.number = value.get<double>();
		break;
	case MOA_FIELD_CHOICE:
	{
		const auto choice =
		    value.is_string() ? std::ranges::find(field.choices, value.get<std::string>()) : field.choices.end();
		if (choice == field.choices.end())
		{
			throw std::runtime_error(std::format("'{}' needs one of its names", field.Path()));
		}
		result.choice = static_cast<uint32_t>(std::distance(field.choices.begin(), choice));
		break;
	}
	}
	return result;
}

/// The state of the settings file and of the settings that the loader uses now. Main thread.
[[nodiscard]] nlohmann::json DescribeSettings(const LoaderState& state)
{
	// The file can differ from the settings in use: a value that needs a restart, or a file with an error.
	const auto path = ModSettingsPath();
	const auto text = ReadTextFile(path);
	const auto file = text ? ParseLoaderSettings(*text)
	                       : moa::Result<LoaderSettings>(moa::MakeError("the file {} cannot be read", path.string()));
	nlohmann::json result {
	    {"path", path.string()},
	    {"live", state.live},
	    {"hotkeysReady", state.hotkeyInput.has_value()},
	    {"current", LoaderSettingsJson(state.settings)},
	    {"requested", LoaderSettingsJson(state.requested)},
	    {"lastReload", state.lastReload},
	    {"messages", state.messages ? state.messages->Describe() : nlohmann::json()},
	};
	result["file"] =
	    file ? nlohmann::json {{"correct", true}, {"restartOnly", CompareLoaderSettings(state.settings, *file).restartOnly}}
	         : nlohmann::json {{"correct", false}, {"error", file.GetError().message}};
	return result;
}

/// Change a field or turn a mod on or off, as the menu bar does. Main thread.
[[nodiscard]] nlohmann::json EditAsMenuBar(LoaderState& state, const nlohmann::json& params)
{
	if (params.contains("mod") || params.contains("dll"))
	{
		const bool zip = params.contains("mod");
		const OverlayCommand command {.kind = zip ? OverlayCommand::Kind::Mod : OverlayCommand::Kind::ModDll,
		                              .on = params.at("enabled").get<bool>(),
		                              .name = params.at(zip ? "mod" : "dll").get<std::string>()};
		ApplyOverlayCommand(state, command);
		return {{"posted", zip ? "mods.disabled" : "modDlls.disabled"}};
	}
	const auto section = params.value("section", std::string());
	const auto key = params.at("key").get<std::string>();
	const auto* field = FindFieldByPath(state, section, key);
	if (field == nullptr)
	{
		throw std::runtime_error("the menu bar has no such field");
	}
	const OverlayCommand command {.kind = OverlayCommand::Kind::Edit,
	                              .id = field->id,
	                              .value = FieldValueOf(*field, params.at("value")),
	                              .save = params.value("save", true)};
	ApplyOverlayCommand(state, command);
	const auto item = ReadField(state, *field);
	return {{"posted", field->Path()}, {"value", FieldJson(*field, item.state.value)}};
}

/// Register the queries of the loader itself (owner 0). The inspector mod calls them on the main thread.
void RegisterLoaderQueries(LoaderState& state)
{
	using Run = std::function<nlohmann::json(LoaderState&, const nlohmann::json&)>;
	struct Item
	{
		const char* name;
		const char* help;
		Run run;
	};
	const std::array items {
	    Item {"mods.status", "The state of the loader, of its parts and of each mod.",
	          [](LoaderState& s, const nlohmann::json& /*params*/) {
		          return nlohmann::json {
		              {"settings", ModSettingsPath().string()},
		              {"frameHook", s.settings.frameHook},
		              {"live", s.live},
		              {"exports", s.managed.has_value()},
		              {"mods", ReadStatuses(s, true)},
		              {"dlls", DescribeDlls(s)},
		              {"overlay", s.overlay ? s.overlay->Describe() : nlohmann::json()},
		          };
	          }},
	    Item {"mods.list", "The mod zips and the mod DLLs, as the Mod list shows them.",
	          [](LoaderState& s, const nlohmann::json& /*params*/) {
		          auto list = DescribeModList(s);
		          list["dlls"] = DescribeDlls(s);
		          return list;
	          }},
	    Item {"mods.settings", "The settings file and the settings that the loader uses now.",
	          [](LoaderState& s, const nlohmann::json& /*params*/) { return DescribeSettings(s); }},
	    Item {"mods.reload", "Read mods.json again and give each mod its objects.",
	          [](LoaderState& s, const nlohmann::json& /*params*/) { return ApplyReload(s, "inspector"); }},
	    Item {"maps.status", "The state of the map override.",
	          [](LoaderState& s, const nlohmann::json& /*params*/) {
		          return s.maps ? s.maps->State() : nlohmann::json {{"installed", false}, {"status", s.mapsStatus.Describe()}};
	          }},
	    Item {"objects.status", "The state of the custom objects. Parameter: filter, a part of a resource path.",
	          [](LoaderState& s, const nlohmann::json& params) {
		          return s.objects ? s.objects->State(params.value("filter", std::string()))
		                           : nlohmann::json {{"installed", false}, {"status", s.objectsStatus.Describe()}};
	          }},
	    Item {"hooks.status", "The method hooks of the loader and the count of the tasks that wait.",
	          [](LoaderState& s, const nlohmann::json& /*params*/) {
		          auto hooks = nlohmann::json::object();
		          for (const auto& [method, hook] : s.hooks)
		          {
			          hooks[method.Text()] = hook->Describe();
		          }
		          const auto frame = s.hooks.find(k_FrameMethod);
		          return nlohmann::json {
		              {"settings", ModSettingsPath().string()},
		              {"frameHook", frame != s.hooks.end() ? frame->second->Describe() : nlohmann::json()},
		              {"hooks", std::move(hooks)},
		              {"quitHook", s.quitHook ? s.quitHook->Describe() : nlohmann::json()},
		              {"pendingTasks", s.tasks.Pending()},
		          };
	          }},
	    Item {"overlay.pin", "Pin the menu bar as its hotkey does. Parameter: pinned (true or false).",
	          [](LoaderState& s, const nlohmann::json& params) {
		          if (!s.overlay)
		          {
			          throw std::runtime_error("the menu bar is not installed (see the log)");
		          }
		          const bool pinned = params.value("pinned", true);
		          if (s.overlay->Describe().value("pinned", false) != pinned)
		          {
			          s.overlayPin = true;
		          }
		          return nlohmann::json {{"pinned", pinned}};
	          }},
	    Item {"overlay.edit",
	          "Change a field as the menu bar does: section, key, value and save. Or turn a mod zip (mod) or a mod DLL "
	          "(dll) on or off with enabled.",
	          [](LoaderState& s, const nlohmann::json& params) { return EditAsMenuBar(s, params); }},
	    Item {"overlay.probe", "Make a new swap chain, as the game does when it changes the display mode.",
	          [](LoaderState& s, const nlohmann::json& /*params*/) {
		          if (!s.overlay)
		          {
			          throw std::runtime_error("the menu bar is not installed (see the log)");
		          }
		          if (auto probe = overlay::DxgiHooks::ProbeAgain(); !probe)
		          {
			          throw std::runtime_error(probe.GetError().message);
		          }
		          return nlohmann::json {{"probe", true}};
	          }},
	};
	const auto trampoline = [](void* user, const char* params, MoaText* result) -> uint32_t {
		auto& query = *static_cast<LoaderQuery*>(user);
		const auto give = [result](const std::string& text) { result->assign(result->context, text.data(), text.size()); };
		// An exception is the normal error of a query: it goes to the caller.
		try
		{
			auto object = nlohmann::json::parse(params == nullptr ? "{}" : params, nullptr, false);
			if (object.is_null())
			{
				object = nlohmann::json::object();
			}
			if (object.is_discarded() || !object.is_object())
			{
				give("the parameters are not a JSON object");
				return 0;
			}
			give(query.run(*query.state, object).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
			return 1;
		}
		catch (const std::exception& error)
		{
			give(error.what());
		}
		catch (...)
		{
			give("an unknown exception in the query");
		}
		return 0;
	};
	for (const auto& item : items)
	{
		auto query = std::make_unique<LoaderQuery>(LoaderQuery {.state = &state, .run = item.run});
		if (auto added = state.registry.AddQuery(k_LoaderOwner, item.name, item.help, trampoline, query.get()); !added)
		{
			spdlog::error("loader: the query {} is refused: {}", item.name, added.GetError().message);
		}
		state.loaderQueries.push_back(std::move(query));
	}
}

/// Show or hide the menu bar for this frame. Before the late start there is no menu bar, and the function does
/// nothing.
void TickOverlay(LoaderState& state, void* gameManager)
{
	constexpr uint64_t k_SnapshotInterval = 15;
	if (!state.overlay)
	{
		return;
	}
	const bool menuOpen = state.menuProbe && state.menuProbe->IsOpen(gameManager);
	const auto change = state.overlay->Tick(menuOpen, std::exchange(state.overlayPin, false));
	if (change.shown)
	{
		state.overlayFrames = 0;
	}
	if (change.visible && state.overlayFrames++ % k_SnapshotInterval == 0)
	{
		// An error here must not stop this function: then the bar does not hide when the menu of the game closes.
		try
		{
			state.overlay->Publish(MakeOverlaySnapshot(state));
		}
		catch (const std::exception& error)
		{
			spdlog::error("menu bar: the state of the mods for the bar failed: {}", error.what());
		}
	}
}

/// Install the menu bar. The late start calls it on the main thread, when the game runs. If a step fails, the log
/// tells why and the game continues with no menu bar.
void InstallOverlay(LoaderState& state)
{
	if (!state.settings.overlay.enabled)
	{
		spdlog::info("menu bar: off in the settings");
		return;
	}
	if (auto probe = GameMenuProbe::Bind(*state.managed); probe)
	{
		state.menuProbe = *probe;
	}
	else
	{
		spdlog::error("menu bar: {}; the bar shows only with its hotkey", probe.GetError().message);
	}
	struct Call
	{
		LoaderState* state;
		std::unique_ptr<Overlay> overlay;
		std::string error;
	} call {&state, nullptr, {}};
	const auto work = [](void* context) {
		auto& c = *static_cast<Call*>(context);
		auto& s = *c.state;
		try
		{
			OverlayHost host {
			    .runOnMainThread =
			        [&s](std::function<void()> task) {
				        (void)s.tasks.Post(MainThreadTasks::Task([task = std::move(task)] {
					        task();
					        return nlohmann::json();
				        }));
			        },
			    .apply = [&s](const OverlayCommand& command) { ApplyOverlayCommand(s, command); },
			};
			if (auto overlay = Overlay::Install(std::move(host)); overlay)
			{
				c.overlay = std::move(*overlay);
			}
			else
			{
				c.error = overlay.GetError().message;
			}
		}
		catch (const std::exception& error)
		{
			c.error = error.what();
		}
		catch (...)
		{
			c.error = "an unknown exception";
		}
	};
	if (!GuardedCall(work, &call, &FilterAccessViolation))
	{
		// The objects of the install can stay in memory. The loader does not use them.
		(void)call.overlay.release();
		spdlog::error("menu bar: an access violation in the install; the menu bar is off");
		return;
	}
	if (!call.overlay)
	{
		spdlog::error("menu bar: {}; the menu bar is off", call.error);
		return;
	}
	state.overlay = std::move(call.overlay);
	spdlog::info("menu bar: ready; it shows with the menu of the game or with its hotkey");
}

/// Do the late steps of the start. The frame hook calls it in each frame, on the main thread.
void RunStartupStep(LoaderState& state)
{
	switch (state.startup.OnFrame())
	{
	case StartupStep::None:
		return;
	case StartupStep::LateStart:
		spdlog::info("start-up: the game ran {} frames; the late start begins", state.startup.Frames());
		WriteStartupMarker(state, "late start");
		if (state.live)
		{
			StartSettingsWatcher(state);
			if (std::ranges::find(state.featuresOff, std::string(k_FeatureOverlay)) != state.featuresOff.end())
			{
				Notify(state, "Mods: the last start did not complete, so the menu bar is off. See the log of the loader.",
				       state.settings.hotkeys.messageSeconds * 2.0);
			}
			else
			{
				WriteStartupMarker(state, "menu bar");
				InstallOverlay(state);
				WriteStartupMarker(state, "after the menu bar");
			}
		}
		spdlog::info("start-up: the late start is complete");
		return;
	case StartupStep::Stable:
		ClearStartupMarker(state, std::format("the game ran {} frames and is stable", state.startup.Frames()));
		return;
	}
}

/// Install the hook that stops the mods and writes the log when the game quits.
void InstallQuitHook(LoaderState& state)
{
	auto hook = MethodHook::Install(state.api, "", "GameManager", "OnApplicationQuit");
	if (!hook)
	{
		spdlog::error("quit hook: {}", hook.GetError().message);
		return;
	}
	state.quitHook = std::move(*hook);
	(void)state.quitHook->Before().Add("quit", [&state] {
		// Each mod puts back the values of the game that it changed.
		if (state.dlls)
		{
			state.dlls->ShutdownAll();
		}
		// The window procedure of the game and the swap chain get their original functions back.
		if (state.overlay)
		{
			state.overlay->Shutdown();
		}
		ClearStartupMarker(state, "the game quits");
		spdlog::info("the game quits");
		spdlog::default_logger()->flush();
	});
}

/// Install one hook on each method that has callbacks, and add the callbacks in the order of the registrations. A
/// method that cannot have a hook writes the reason to the log: the callbacks of the mods on it do not run.
void InstallMethodHooks(LoaderState& state)
{
	std::vector<MethodKey> methods {k_FrameMethod};
	for (const auto& entry : state.registry.MethodCallbacks())
	{
		if (std::ranges::find(methods, entry.method) == methods.end() && !state.registry.Failed(entry.owner))
		{
			methods.push_back(entry.method);
		}
	}
	for (const auto& method : methods)
	{
		auto hook = MethodHook::Install(state.api, method.space.c_str(), method.className.c_str(), method.method.c_str(),
		                                method.parameterTypes);
		if (!hook)
		{
			spdlog::error("hook: {}: {}", method.Text(), hook.GetError().message);
			for (const auto& entry : state.registry.MethodCallbacks())
			{
				if (entry.method == method)
				{
					spdlog::error("[{}] the callback '{}' does not run: no hook on {}", state.registry.OwnerName(entry.owner),
					              entry.name, method.Text());
				}
			}
			continue;
		}
		auto* installed = hook->get();
		state.hooks.emplace(method, std::move(*hook));
		for (const auto& entry : state.registry.MethodCallbacks())
		{
			if (entry.method != method)
			{
				continue;
			}
			auto& list = entry.phase == MOA_HOOK_BEFORE ? installed->Before() : installed->After();
			const auto name = std::format("{}:{}", state.registry.OwnerName(entry.owner), entry.name);
			const auto added = list.Add(name, [&state, installed, entry] {
				const MoaCall call {.instance = installed->Instance(),
				                    .arguments = {installed->Argument(0), installed->Argument(1)}};
				auto work = [&] { entry.call(entry.user, &call); };
				(void)CallMod(state, entry.owner, "a method callback", work);
			});
			if (!added)
			{
				spdlog::error("hook: the callback {} on {} exists", name, method.Text());
			}
		}
	}
}

/// Add the frame work of the loader: the heartbeat, the hotkeys, the menu bar, the late start and the tasks.
void AddFrameWork(LoaderState& state)
{
	const auto found = state.hooks.find(k_FrameMethod);
	if (found == state.hooks.end())
	{
		spdlog::error("frame hook: no hook on {}; the hotkeys, the menu bar and the tasks are off", k_FrameMethod.Text());
		return;
	}
	auto& frameHook = *found->second;
	if (state.settings.frameHook)
	{
		constexpr uint64_t k_LogInterval = 3600;
		(void)frameHook.After().Add("heartbeat", [count = uint64_t {0}]() mutable {
			if (count++ % k_LogInterval == 0)
			{
				spdlog::info("frame hook: frame {} on thread {}", count, GetCurrentThreadId());
			}
		});
	}
	if (state.live)
	{
		(void)frameHook.After().Add("controls", [&state] { ReadControls(state); });
		// The menu bar starts late. Its function is in the list from the start, because a function of the list must
		// not add a function.
		(void)frameHook.After().Add("overlay", [&state, &frameHook] { TickOverlay(state, frameHook.Instance()); });
	}
	(void)frameHook.After().Add("startup", [&state] { RunStartupStep(state); });
	(void)frameHook.After().Add("tasks", [&state] {
		state.mainThread = GetCurrentThreadId();
		state.tasks.RunPending();
	});
}

/// Install the hook on the resource loads of the game for the custom objects.
void InstallObjects(LoaderState& state)
{
	const auto& settings = state.settings.objects;
	const auto folder = settings.traceFolder.empty() ? state.logFolder : PathOf(settings.traceFolder);
	if (IsInGameFolder(folder))
	{
		spdlog::error("objects: the trace folder {} is in a game folder or not correct; the objects are off", folder.string());
		state.objectsStatus.SetError("the trace folder is not correct");
		return;
	}
	std::error_code error;
	const auto absolute = std::filesystem::absolute(folder, error);
	const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
	const auto file = absolute / std::format("objects-loads-{:%Y%m%d-%H%M%S}-{}.tsv", now, GetCurrentProcessId());
	// The packs of the mod zips that load, in the load order, and the development folder when the objects are on.
	std::vector<objects::PackOrigin> packs;
	for (const auto* mod : state.catalogue.Loaded())
	{
		for (const auto& packFolder : mod->info->packs)
		{
			const auto prefix = std::format("{}/{}/", moa::modzip::k_PacksFolder, packFolder);
			auto text = mod->archive->Read(prefix + "pack.json");
			packs.push_back(objects::PackOrigin {
			    .key = std::format("mods/{}/{}", mod->info->id, packFolder),
			    .text = text ? std::string(reinterpret_cast<const char*>(text->data()), text->size()) : std::string(),
			    .read = mods::ZipFolderReader(mod->archive, prefix),
			});
		}
	}
	if (settings.enabled)
	{
		const auto packsFolder = settings.packsFolder.empty() ? SettingsFolder() / "packs" : PathOf(settings.packsFolder);
		auto loose = objects::ReadPackFolder(packsFolder);
		spdlog::info("objects: {} packs in the development folder {}", loose.size(), packsFolder.string());
		std::ranges::move(loose, std::back_inserter(packs));
	}
	// The trace of the loads is for research: only the objects setting turns it on.
	auto objectSettings = settings;
	objectSettings.trace = settings.enabled && settings.trace;
	auto mod = objects::ObjectsMod::Install(*state.managed, objectSettings, file, std::move(packs), state.objectsStatus);
	if (!mod)
	{
		spdlog::error("objects: {}; the objects are off", mod.GetError().message);
		state.objectsStatus.SetError(mod.GetError().message);
		return;
	}
	state.objects = std::move(*mod);
	state.objectsStatus.SetReady(true);
}

/// Read a file of a development folder of the map overrides.
[[nodiscard]] moa::Result<std::vector<std::byte>> ReadOverrideFile(const std::filesystem::path& folder,
                                                                   const std::string& relative)
{
	const auto path = folder / PathOf(relative);
	std::error_code error;
	const auto size = std::filesystem::file_size(path, error);
	if (error)
	{
		return moa::MakeError("{} cannot be read: {}", path.string(), error.message());
	}
	if (size > maps::k_MaxOverrideFileBytes)
	{
		return moa::MakeError("{} has {} bytes; the loader reads not more than {}", path.string(), size,
		                      maps::k_MaxOverrideFileBytes);
	}
	std::vector<std::byte> bytes(static_cast<size_t>(size));
	std::ifstream stream(path, std::ios::binary);
	if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
	{
		return moa::MakeError("{} cannot be read", path.string());
	}
	return bytes;
}

/// Read and check the map override of the settings, and install the hook on the resource loads of the game. If the
/// override is for another game build or a file is not correct, the game gets nothing of it.
void InstallMaps(LoaderState& state)
{
	const auto& choice = state.catalogue.map;
	const auto& name = choice.name;
	maps::OverrideFileReader read;
	if (choice.kind == mods::MapChoice::Kind::Mod)
	{
		const auto* mod = state.catalogue.FindLoaded(name);
		if (mod == nullptr)
		{
			state.mapsStatus.SetError(std::format("the mod {} does not load", name));
			return;
		}
		spdlog::info("maps: the map of the mod '{}' ({})", name, mod->file);
		read = mods::ZipFolderReader(mod->archive, std::string(moa::modzip::k_MapFolder) + "/");
	}
	else
	{
		const auto folder = MapsFolder() / PathOf(name);
		if (IsInGameFolder(folder))
		{
			spdlog::error("maps: the folder {} is in a game folder; the override is not used", folder.string());
			state.mapsStatus.SetError("the folder of the override is in the game folder");
			return;
		}
		spdlog::info("maps: the map of the development folder {}", folder.string());
		read = [folder](const std::string& relative) { return ReadOverrideFile(folder, relative); };
	}
	auto set = maps::MapOverrideSet::Load(name, state.buildGuid, read);
	if (!set)
	{
		spdlog::error("maps: the override '{}' is not used, and the game loads its own data: {}", name, set.GetError().message);
		state.mapsStatus.SetError(set.GetError().message);
		return;
	}
	spdlog::info("maps: the override '{}' (build {}, made {} by {}) has {} items", name, set->Manifest().buildGuid,
	             set->Manifest().made, set->Manifest().tool, set->Items().size());
	auto mod = maps::MapsMod::Install(*state.managed, std::move(*set), state.mapsStatus);
	if (!mod)
	{
		spdlog::error("maps: {}; the override is not used", mod.GetError().message);
		state.mapsStatus.SetError(mod.GetError().message);
		return;
	}
	state.maps = std::move(*mod);
	if (state.objects)
	{
		// The map override gives some tables in place of the game texts. The rows of the packs must be in them too.
		state.maps->SetTextFilter([objects = state.objects.get()](const std::string& path, const std::string& text) {
			return objects->FilterTable(path, text);
		});
	}
	state.mapsStatus.SetReady(true);
}

/// Load each mod DLL of the folder `mods`, except the DLLs that the settings or the start-up guard turn off.
void LoadModDlls(LoaderState& state, const std::vector<std::filesystem::path>& files)
{
	HostInfo info {.loaderVersion = MoaModLoaderVersion(),
	               .buildGuid = state.buildGuid,
	               .gameFolder = Utf8(GameFolder()),
	               .settingsFolder = Utf8(SettingsFolder()),
	               .logFolder = Utf8(state.logFolder)};
	state.dlls = std::make_unique<ModDllLoader>(state, std::move(info), &FilterAccessViolation);
	spdlog::info("mod DLLs: {} in {}", files.size(), ModsFolder().string());
	for (const auto& file : files)
	{
		const auto name = Utf8(file.filename());
		const auto& disabled = state.settings.modDlls.disabled;
		if (std::ranges::find(disabled, name) != disabled.end())
		{
			const auto& mod = state.dlls->AddDisabled(file, "modDlls.disabled in mods.json");
			spdlog::info("mod DLLs:   {} is off ({})", mod.file, mod.error);
			continue;
		}
		WriteStartupMarker(state, std::format("mod DLL {}", name));
		const auto& mod = state.dlls->Load(file);
		spdlog::info("mod DLLs:   {} is {}{}", mod.file, ModDllStateName(mod.state),
		             mod.error.empty() ? std::string() : ": " + mod.error);
	}
}

void Start()
{
	const auto logFolder = LogFolder();
	if (IsInGameFolder(logFolder))
	{
		// The loader writes nothing into a game folder, also no log.
		return;
	}
	StartLog(logFolder);
	spdlog::info("the mod loader {} starts in process {}", MoaModLoaderVersion(), GetCurrentProcessId());
	// MOA_CRASH_DUMPS=0 turns off the crash dump writer.
	if (ReadEnvironment("MOA_CRASH_DUMPS", "1") != "0")
	{
		if (auto dumps = InstallCrashDumpWriter(SettingsFolder() / "crash-reports"); !dumps)
		{
			spdlog::warn("crash dump: {}; a crash writes no dump", dumps.GetError().message);
		}
	}
	if (!WaitForIl2Cpp())
	{
		spdlog::error("IL2CPP did not start in time; the loader does nothing");
		return;
	}
	auto api = Il2CppApi::Load();
	if (!api)
	{
		spdlog::error("{}", api.GetError().message);
		return;
	}
	// The hooks use the state until the process ends, so the loader gives the state to the process.
	auto* state = std::make_unique<LoaderState>().release();
	state->api = std::move(*api);
	state->logFolder = logFolder;
	LoadSettingsFile(*state);
	state->live = state->settings.liveReload && std::filesystem::exists(ModSettingsPath());
	state->buildGuid = ReadBuildGuid();
	s_state = state;
	spdlog::info("build {}", state->buildGuid);
	LoadCatalogue(*state);
	const auto dllFiles = FindModDlls(ModsFolder());
	BeginStartupGuard(*state, dllFiles);
	{
		// Stay attached only for the install. IL2CPP waits at shutdown for each attached thread.
		const ThreadAttachment attachment(state->api);
		auto managed = Managed::Load(state->api);
		if (!managed)
		{
			spdlog::error("loader: {}; no mod runs", managed.GetError().message);
			ClearStartupMarker(*state, "no mod runs");
			return;
		}
		state->managed = *managed;
		const bool packs =
		    std::ranges::any_of(state->catalogue.Loaded(), [](const auto* mod) { return !mod->info->packs.empty(); });
		const bool objects = state->settings.objects.enabled || packs;
		const bool map = state->catalogue.map.kind != mods::MapChoice::Kind::None;
		state->objectsStatus.SetEnabled(objects);
		state->mapsStatus.SetEnabled(map);
		// The objects first: the game must not load its tables before the hook is there.
		if (objects)
		{
			InstallObjects(*state);
		}
		// The map override after the objects: its hook calls the hook of the objects.
		if (map)
		{
			InstallMaps(*state);
		}
		RegisterLoaderItems(*state);
		RegisterLoaderQueries(*state);
		LoadModDlls(*state, dllFiles);
		(void)DeliverSettings(*state);
		if (state->live)
		{
			InstallControls(*state);
		}
		WriteStartupMarker(*state, "hooks");
		InstallMethodHooks(*state);
		AddFrameWork(*state);
		InstallQuitHook(*state);
	}
	if (!state->hooks.contains(k_FrameMethod))
	{
		// No frame hook counts the frames, and no feature that the marker protects can start.
		ClearStartupMarker(*state, "no frame hook runs");
	}
	spdlog::info("the start of the loader is complete");
}

DWORD WINAPI StartThread(void* /*parameter*/)
{
	// No C++ exception may leave the loader into the game.
	try
	{
		Start();
	}
	catch (const std::exception& error)
	{
		spdlog::error("the loader stopped: {}", error.what());
	}
	catch (...)
	{
		spdlog::error("the loader stopped with an unknown exception");
	}
	return 0;
}

} // namespace

/// Put the original code pointers back. `DllMain` calls it when the program unloads the DLL. The loader lock is held,
/// so the function only does atomic writes, changes of memory protection and the change of the window procedure: no
/// log, no file, no lock and no memory allocation.
void RemoveHooksOnUnload()
{
	if (auto* state = s_state.load(); state != nullptr)
	{
		for (auto& [method, hook] : state->hooks)
		{
			hook->Remove(false);
		}
		if (state->quitHook)
		{
			state->quitHook->Remove(false);
		}
		if (state->maps)
		{
			state->maps->Remove(false);
		}
		if (state->objects)
		{
			state->objects->Remove(false);
		}
		// No call may reach the window procedure or the swap chain hooks of the menu bar after the unload.
		if (state->overlay)
		{
			state->overlay->Shutdown();
		}
	}
}

} // namespace moa_mod::loader

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
	if (reason == DLL_PROCESS_DETACH)
	{
		// When the process ends, the other threads stopped at a random point and can hold locks. The DLL then does
		// nothing: the quit hook already stopped the mods. When a program unloads the DLL, no call may go to its code
		// after the unload.
		if (reserved == nullptr)
		{
			moa_mod::loader::RemoveHooksOnUnload();
		}
		return TRUE;
	}
	if (reason != DLL_PROCESS_ATTACH)
	{
		return TRUE;
	}
	DisableThreadLibraryCalls(instance);
	if (!moa_mod::loader::LoadSystemVersion())
	{
		return FALSE;
	}
	// MOA_MOD_LOADER=0 keeps only the proxy, so that the game runs as if the loader were not there.
	if (moa_mod::loader::IsGameProcess() && moa_mod::loader::ReadEnvironment("MOA_MOD_LOADER", "1") != "0")
	{
		if (const auto thread = CreateThread(nullptr, 0, moa_mod::loader::StartThread, nullptr, 0, nullptr); thread != nullptr)
		{
			CloseHandle(thread);
		}
	}
	return TRUE;
}
