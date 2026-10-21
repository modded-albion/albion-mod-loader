/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <atomic>
#include <string>
#include <string_view>

#include <moa/common/Result.h>
#include <moa_mod/api/ModApi.h>
#include <moa_mod/loader/ModRegistry.h>

namespace moa_mod::loader
{

/// The work of the loader behind the table of a mod. The game side gives the real one; the tests give a fake.
class HostServices
{
public:
	virtual ~HostServices() = default;

	/// The registrations of all mods.
	[[nodiscard]] virtual ModRegistry& Registry() = 0;
	virtual void Log(OwnerId owner, MoaLogLevel level, std::string_view text) = 0;
	[[nodiscard]] virtual moa::Result<void> PostTask(OwnerId owner, MoaTaskFn task, void* user) = 0;
	[[nodiscard]] virtual moa::Result<void> SaveSettings(OwnerId owner, std::string_view patch) = 0;
	virtual void ShowMessage(OwnerId owner, std::string_view text, double seconds) = 0;
};

/// The texts of the table. They stay valid as long as the table.
struct HostInfo
{
	std::string loaderVersion;
	std::string buildGuid;
	std::string gameFolder;
	std::string settingsFolder;
	std::string logFolder;
};

/// The table (`MoaHostApi`) that the loader gives to one mod. Each function of the table finds this object through
/// `context`, catches every exception, and writes each refusal to the log with the name of the mod.
///
/// The registrations are open only during `MoaModInit`: after `Close`, the table refuses them. The log, the tasks, the
/// changes of mods.json and the messages work until the process ends.
class HostTable
{
public:
	HostTable(HostServices& services, OwnerId owner, HostInfo info);
	HostTable(const HostTable&) = delete;
	HostTable& operator=(const HostTable&) = delete;

	[[nodiscard]] const MoaHostApi& Api() const { return _api; }
	[[nodiscard]] OwnerId Owner() const { return _owner; }

	/// Refuse all registrations from now on.
	void Close() { _open = false; }
	[[nodiscard]] bool Open() const { return _open; }

	/// The count of the registrations that the table refused.
	[[nodiscard]] size_t Refusals() const { return _refusals; }

	/// Used by the functions of the table.
	[[nodiscard]] HostServices& Services() const { return _services; }
	/// Log a refusal of a registration. Return 0, the result of a refused registration.
	uint32_t Refuse(std::string_view what, const moa::Error& error);

private:
	HostServices& _services;
	OwnerId _owner;
	HostInfo _info;
	MoaHostApi _api {};
	std::atomic<bool> _open = true;
	std::atomic<size_t> _refusals = 0;
};

} // namespace moa_mod::loader
