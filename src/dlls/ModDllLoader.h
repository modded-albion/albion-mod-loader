/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <common/Result.h>
#include <core/HostTable.h>
#include <core/ModRegistry.h>
#include <mod_api/ModApi.h>

struct _EXCEPTION_POINTERS;

namespace moa_mod::loader
{

/// The state of one mod DLL in the folder `mods`.
enum class ModDllState
{
	Loaded,   ///< The init of the mod completed.
	Disabled, ///< mods.json or the start-up guard turned it off.
	Refused,  ///< The file, its exports, its interface version or its init was not correct. The DLL is not loaded.
	Failed,   ///< An access violation in the init or later. The calls to the mod stop.
};

[[nodiscard]] const char* ModDllStateName(ModDllState state);

/// One mod DLL.
struct ModDll
{
	std::string file;
	ModDllState state = ModDllState::Refused;
	std::string error;
	OwnerId owner = 0;
	std::string id;
	std::string name;
	std::string version;
	void* module = nullptr;
	MoaModShutdownFn shutdown = nullptr;
	std::unique_ptr<HostTable> table;
};

/// The filter of a guard around a call into a mod: return EXCEPTION_EXECUTE_HANDLER (1) to catch the exception.
using ExceptionFilter = int (*)(_EXCEPTION_POINTERS* pointers);

/// Call `function(context)`. Return false if an exception that `filter` catches stops it (for example an access
/// violation in a mod). The stack then unwinds without destructors, so the caller stops all calls to that mod.
[[nodiscard]] bool GuardedCall(void (*function)(void*), void* context, ExceptionFilter filter);

/// The filter of `GuardedCall` that catches only access violations.
[[nodiscard]] int CatchAccessViolation(_EXCEPTION_POINTERS* pointers);

/// Return the DLL files in a folder, in the order of their names. A link or a folder with the extension is not a DLL.
[[nodiscard]] std::vector<std::filesystem::path> FindModDlls(const std::filesystem::path& folder);

/// Loads the mod DLLs and calls their exports. It never lets a bad mod stop the game: a mod with a missing export, a
/// different interface version, a bad id or a failed init is not used, and its registrations go.
class ModDllLoader
{
public:
	ModDllLoader(HostServices& services, HostInfo info, ExceptionFilter filter = &CatchAccessViolation);
	ModDllLoader(const ModDllLoader&) = delete;
	ModDllLoader& operator=(const ModDllLoader&) = delete;

	/// Load one DLL and call its init with a new owner. The result tells the state; the list keeps it.
	const ModDll& Load(const std::filesystem::path& path);
	/// Add a DLL that does not load, for the list of the menu bar.
	const ModDll& AddDisabled(const std::filesystem::path& path, std::string reason);

	/// Call `MoaModShutdown` of each loaded mod, in the reverse order of the loads. The DLLs stay in memory.
	void ShutdownAll();

	/// Stop the calls to a mod after an error in its code.
	void MarkFailed(OwnerId owner, const std::string& reason);

	[[nodiscard]] const std::vector<std::unique_ptr<ModDll>>& Mods() const { return _mods; }
	[[nodiscard]] const ModDll* Find(OwnerId owner) const;

private:
	ModDll& Refuse(ModDll& mod, std::string error, bool unload);

	HostServices& _services;
	HostInfo _info;
	ExceptionFilter _filter;
	std::vector<std::unique_ptr<ModDll>> _mods;
	OwnerId _nextOwner = 1;
	bool _shutDown = false;
};

} // namespace moa_mod::loader
