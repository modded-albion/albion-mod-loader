/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <exception>
#include <format>
#include <utility>

#include <moa_mod/loader/HostTable.h>

namespace moa_mod::loader
{

namespace
{
[[nodiscard]] HostTable& TableOf(void* context)
{
	return *static_cast<HostTable*>(context);
}

[[nodiscard]] std::string Copy(const char* text)
{
	return text == nullptr ? std::string() : std::string(text);
}

/// Run `work` and turn an exception into a refusal. No exception may go back into the mod.
template <typename Work>
uint32_t Guard(HostTable& table, std::string_view what, Work&& work) noexcept
{
	try
	{
		return work();
	}
	catch (const std::exception& error)
	{
		return table.Refuse(what, moa::Error {error.what()});
	}
	catch (...)
	{
		return table.Refuse(what, moa::Error {"an unknown exception"});
	}
}

/// The registrations are open only during the init of the mod.
template <typename Work>
uint32_t Register(void* context, std::string_view what, Work&& work) noexcept
{
	if (context == nullptr)
	{
		return 0;
	}
	auto& table = TableOf(context);
	return Guard(table, what, [&]() -> uint32_t {
		if (!table.Open())
		{
			return table.Refuse(what, moa::MakeError("the mod can register only in MoaModInit"));
		}
		const moa::Result<void> result = work(table);
		return result ? 1u : table.Refuse(what, result.GetError());
	});
}

void Log(void* context, MoaLogLevel level, const char* text)
{
	if (context == nullptr || text == nullptr)
	{
		return;
	}
	auto& table = TableOf(context);
	try
	{
		table.Services().Log(table.Owner(), level, text);
	}
	catch (...)
	{
		// The log has no other place for its own error.
	}
}

uint32_t AddMethodCallback(void* context, const MoaMethodDesc* method, MoaHookPhase phase, const char* name, MoaCallFn callback,
                           void* user)
{
	return Register(context, std::format("the method callback '{}'", Copy(name)), [&](HostTable& table) -> moa::Result<void> {
		if (method == nullptr)
		{
			return moa::MakeError("no method");
		}
		MethodKey key {.space = Copy(method->space),
		               .className = Copy(method->className),
		               .method = Copy(method->method),
		               .parameterTypes = {}};
		for (uint32_t i = 0; i < method->parameterCount; ++i)
		{
			key.parameterTypes.push_back(method->parameterTypes == nullptr ? std::string() : Copy(method->parameterTypes[i]));
		}
		return table.Services().Registry().AddMethodCallback(table.Owner(), std::move(key), phase, Copy(name), callback, user);
	});
}

uint32_t PostTask(void* context, MoaTaskFn task, void* user)
{
	if (context == nullptr)
	{
		return 0;
	}
	auto& table = TableOf(context);
	return Guard(table, "a task", [&]() -> uint32_t {
		if (task == nullptr)
		{
			return table.Refuse("a task", moa::MakeError("no function"));
		}
		const auto result = table.Services().PostTask(table.Owner(), task, user);
		return result ? 1u : table.Refuse("a task", result.GetError());
	});
}

uint32_t AddSettings(void* context, const char* section, MoaSettingsFn load, void* user)
{
	return Register(context, std::format("the settings section '{}'", Copy(section)), [&](HostTable& table) {
		return table.Services().Registry().AddSettings(table.Owner(), Copy(section), load, user);
	});
}

uint32_t SaveSettings(void* context, const char* patch)
{
	if (context == nullptr)
	{
		return 0;
	}
	auto& table = TableOf(context);
	return Guard(table, "a change of mods.json", [&]() -> uint32_t {
		const auto result = table.Services().SaveSettings(table.Owner(), patch == nullptr ? "" : patch);
		return result ? 1u : table.Refuse("a change of mods.json", result.GetError());
	});
}

uint32_t AddField(void* context, const MoaFieldDesc* field, MoaFieldGetFn get, MoaFieldSetFn set, void* user)
{
	const auto what = field == nullptr ? std::string("a field") : std::format("the field '{}'", Copy(field->key));
	return Register(context, what, [&](HostTable& table) -> moa::Result<void> {
		if (field == nullptr)
		{
			return moa::MakeError("no description");
		}
		auto id = table.Services().Registry().AddField(table.Owner(), *field, get, set, user);
		if (!id)
		{
			return id.GetError();
		}
		return {};
	});
}

uint32_t AddAction(void* context, const MoaActionDesc* action, MoaTaskFn run, MoaEnabledFn enabled, void* user)
{
	const auto what = action == nullptr
	                      ? std::string("an action")
	                      : std::format("the action '{}'", Copy(action->hotkey != nullptr ? action->hotkey : action->label));
	return Register(context, what, [&](HostTable& table) -> moa::Result<void> {
		if (action == nullptr)
		{
			return moa::MakeError("no description");
		}
		auto id = table.Services().Registry().AddAction(table.Owner(), *action, run, enabled, user);
		if (!id)
		{
			return id.GetError();
		}
		return {};
	});
}

uint32_t AddReadout(void* context, const char* menu, const char* group, MoaReadFn read, void* user)
{
	return Register(context, std::format("the readout '{}'", Copy(group)), [&](HostTable& table) -> moa::Result<void> {
		auto id = table.Services().Registry().AddReadout(table.Owner(), menu, group, read, user);
		if (!id)
		{
			return id.GetError();
		}
		return {};
	});
}

uint32_t SetStatusReader(void* context, MoaReadFn read, void* user)
{
	return Register(context, "the status reader",
	                [&](HostTable& table) { return table.Services().Registry().SetStatusReader(table.Owner(), read, user); });
}

void ShowMessage(void* context, const char* text, double seconds)
{
	if (context == nullptr || text == nullptr)
	{
		return;
	}
	auto& table = TableOf(context);
	(void)Guard(table, "a message", [&]() -> uint32_t {
		table.Services().ShowMessage(table.Owner(), text, seconds);
		return 1;
	});
}
} // namespace

HostTable::HostTable(HostServices& services, OwnerId owner, HostInfo info)
    : _services(services)
    , _owner(owner)
    , _info(std::move(info))
{
	_api.apiVersion = MOA_MOD_API_VERSION;
	_api.size = sizeof(MoaHostApi);
	_api.context = this;
	_api.loaderVersion = _info.loaderVersion.c_str();
	_api.buildGuid = _info.buildGuid.c_str();
	_api.gameFolder = _info.gameFolder.c_str();
	_api.settingsFolder = _info.settingsFolder.c_str();
	_api.logFolder = _info.logFolder.c_str();
	_api.log = &Log;
	_api.addMethodCallback = &AddMethodCallback;
	_api.postTask = &PostTask;
	_api.addSettings = &AddSettings;
	_api.saveSettings = &SaveSettings;
	_api.addField = &AddField;
	_api.addAction = &AddAction;
	_api.addReadout = &AddReadout;
	_api.setStatusReader = &SetStatusReader;
	_api.showMessage = &ShowMessage;
}

uint32_t HostTable::Refuse(std::string_view what, const moa::Error& error)
{
	++_refusals;
	try
	{
		_services.Log(_owner, MOA_LOG_ERROR, std::format("the loader refuses {}: {}", what, error.message));
	}
	catch (...)
	{
		// The log has no other place for its own error.
	}
	return 0;
}

} // namespace moa_mod::loader
