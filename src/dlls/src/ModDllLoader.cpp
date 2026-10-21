/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <algorithm>
#include <format>
#include <ranges>
#include <utility>

#include <moa_mod/loader/ModDllLoader.h>
#include <windows.h>

namespace moa_mod::loader
{

namespace
{
/// An id of letters, digits, '-' and '_', at most 64 characters.
[[nodiscard]] bool IsModId(std::string_view id)
{
	return !id.empty() && id.size() <= 64 && std::ranges::all_of(id, [](char c) {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
	});
}

[[nodiscard]] std::string Utf8(const std::filesystem::path& path)
{
	const auto text = path.u8string();
	return {text.begin(), text.end()};
}

[[nodiscard]] std::string Copy(const char* text, size_t limit)
{
	if (text == nullptr)
	{
		return {};
	}
	const auto* end = std::find(text, text + limit, '\0');
	return {text, end};
}

/// The context of the guarded calls.
struct ApiVersionCall
{
	MoaModApiVersionFn function;
	uint32_t result;
};

struct InfoCall
{
	MoaModGetInfoFn function;
	std::string id;
	std::string name;
	std::string version;
};

struct InitCall
{
	MoaModInitFn function;
	const MoaHostApi* api;
	uint32_t result;
};
} // namespace

const char* ModDllStateName(ModDllState state)
{
	switch (state)
	{
	case ModDllState::Loaded:
		return "loaded";
	case ModDllState::Disabled:
		return "off";
	case ModDllState::Refused:
		return "refused";
	case ModDllState::Failed:
		return "failed";
	}
	return "unknown";
}

bool GuardedCall(void (*function)(void*), void* context, ExceptionFilter filter)
{
	__try
	{
		function(context);
		return true;
	}
	__except (filter(GetExceptionInformation()))
	{
		return false;
	}
}

int CatchAccessViolation(_EXCEPTION_POINTERS* pointers)
{
	return pointers != nullptr && pointers->ExceptionRecord != nullptr &&
	               pointers->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
	           ? EXCEPTION_EXECUTE_HANDLER
	           : EXCEPTION_CONTINUE_SEARCH;
}

std::vector<std::filesystem::path> FindModDlls(const std::filesystem::path& folder)
{
	std::vector<std::filesystem::path> files;
	std::error_code error;
	for (const auto& entry : std::filesystem::directory_iterator(folder, error))
	{
		auto extension = entry.path().extension().string();
		std::ranges::transform(extension, extension.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });
		std::error_code kindError;
		if (extension == ".dll" && !entry.is_symlink(kindError) && entry.is_regular_file(kindError))
		{
			files.push_back(entry.path());
		}
	}
	std::ranges::sort(files, {}, [](const std::filesystem::path& path) { return path.filename().wstring(); });
	return files;
}

ModDllLoader::ModDllLoader(HostServices& services, HostInfo info, ExceptionFilter filter)
    : _services(services)
    , _info(std::move(info))
    , _filter(filter)
{
}

ModDll& ModDllLoader::Refuse(ModDll& mod, std::string error, bool unload)
{
	_services.Registry().RemoveOwner(mod.owner);
	mod.state = ModDllState::Refused;
	mod.error = std::move(error);
	mod.shutdown = nullptr;
	if (unload && mod.module != nullptr)
	{
		FreeLibrary(static_cast<HMODULE>(mod.module));
		mod.module = nullptr;
	}
	_services.Log(mod.owner, MOA_LOG_ERROR, std::format("mod DLL {} is not used: {}", mod.file, mod.error));
	return mod;
}

const ModDll& ModDllLoader::AddDisabled(const std::filesystem::path& path, std::string reason)
{
	auto mod = std::make_unique<ModDll>();
	mod->file = Utf8(path.filename());
	mod->state = ModDllState::Disabled;
	mod->error = std::move(reason);
	_mods.push_back(std::move(mod));
	return *_mods.back();
}

const ModDll& ModDllLoader::Load(const std::filesystem::path& path)
{
	auto& mod = *_mods.emplace_back(std::make_unique<ModDll>());
	mod.file = Utf8(path.filename());
	mod.owner = _nextOwner++;
	_services.Registry().SetOwnerName(mod.owner, mod.file);

	// Only the folder of the DLL and the system folders give the DLLs that the mod needs.
	const auto module =
	    LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	if (module == nullptr)
	{
		return Refuse(mod, std::format("Windows cannot load it (error {})", GetLastError()), false);
	}
	mod.module = module;
	const auto apiVersion = reinterpret_cast<MoaModApiVersionFn>(GetProcAddress(module, MOA_MOD_EXPORT_API_VERSION));
	const auto getInfo = reinterpret_cast<MoaModGetInfoFn>(GetProcAddress(module, MOA_MOD_EXPORT_GET_INFO));
	const auto init = reinterpret_cast<MoaModInitFn>(GetProcAddress(module, MOA_MOD_EXPORT_INIT));
	const auto shutdown = reinterpret_cast<MoaModShutdownFn>(GetProcAddress(module, MOA_MOD_EXPORT_SHUTDOWN));
	if (apiVersion == nullptr || getInfo == nullptr || init == nullptr || shutdown == nullptr)
	{
		return Refuse(mod,
		              std::format("it is not a mod: it must export {}, {}, {} and {}", MOA_MOD_EXPORT_API_VERSION,
		                          MOA_MOD_EXPORT_GET_INFO, MOA_MOD_EXPORT_INIT, MOA_MOD_EXPORT_SHUTDOWN),
		              true);
	}

	ApiVersionCall versionCall {.function = apiVersion, .result = 0};
	if (!GuardedCall(
	        [](void* c) {
		        auto& call = *static_cast<ApiVersionCall*>(c);
		        call.result = call.function();
	        },
	        &versionCall, _filter))
	{
		return Refuse(mod, "an access violation in MoaModApiVersion", false);
	}
	if (versionCall.result != MOA_MOD_API_VERSION)
	{
		return Refuse(
		    mod, std::format("it uses the interface version {}; this loader uses {}", versionCall.result, MOA_MOD_API_VERSION),
		    true);
	}

	InfoCall infoCall {.function = getInfo, .id = {}, .name = {}, .version = {}};
	if (!GuardedCall(
	        [](void* c) {
		        auto& call = *static_cast<InfoCall*>(c);
		        const auto* info = call.function();
		        if (info != nullptr)
		        {
			        call.id = Copy(info->id, 65);
			        call.name = Copy(info->name, 128);
			        call.version = Copy(info->version, 64);
		        }
	        },
	        &infoCall, _filter))
	{
		return Refuse(mod, "an access violation in MoaModGetInfo", false);
	}
	if (!IsModId(infoCall.id))
	{
		return Refuse(mod, std::format("the id '{}' must be 1 to 64 letters, digits, '-' and '_'", infoCall.id), true);
	}
	const auto same = std::ranges::find_if(_mods, [&](const std::unique_ptr<ModDll>& other) {
		return other.get() != &mod && other->state == ModDllState::Loaded && other->id == infoCall.id;
	});
	if (same != _mods.end())
	{
		return Refuse(mod, std::format("the mod {} in {} has the same id", infoCall.id, (*same)->file), true);
	}
	mod.id = infoCall.id;
	mod.name = infoCall.name.empty() ? infoCall.id : infoCall.name;
	mod.version = infoCall.version;
	_services.Registry().SetOwnerName(mod.owner, mod.name);

	mod.table = std::make_unique<HostTable>(_services, mod.owner, _info);
	InitCall initCall {.function = init, .api = &mod.table->Api(), .result = 0};
	const bool completed = GuardedCall(
	    [](void* c) {
		    auto& call = *static_cast<InitCall*>(c);
		    call.result = call.function(call.api, MOA_MOD_API_VERSION);
	    },
	    &initCall, _filter);
	mod.table->Close();
	if (!completed)
	{
		// The code of the mod can still be in use (a thread, a pointer), so the DLL stays in memory.
		Refuse(mod, "an access violation in MoaModInit", false);
		mod.state = ModDllState::Failed;
		return mod;
	}
	if (initCall.result == 0)
	{
		return Refuse(mod, "its MoaModInit returned 0 (see the lines of the mod above)", true);
	}
	mod.shutdown = shutdown;
	mod.state = ModDllState::Loaded;
	_services.Log(mod.owner, MOA_LOG_INFO,
	              std::format("mod DLL {} loaded: {} {} ({}), {} refused registrations", mod.file, mod.name, mod.version,
	                          mod.id, mod.table->Refusals()));
	return mod;
}

void ModDllLoader::ShutdownAll()
{
	if (std::exchange(_shutDown, true))
	{
		return;
	}
	for (auto& mod : std::ranges::reverse_view(_mods))
	{
		if (mod->state != ModDllState::Loaded || mod->shutdown == nullptr)
		{
			continue;
		}
		if (!GuardedCall([](void* c) { (*static_cast<MoaModShutdownFn*>(c))(); }, &mod->shutdown, _filter))
		{
			_services.Log(mod->owner, MOA_LOG_ERROR, std::format("an access violation in MoaModShutdown of {}", mod->file));
		}
	}
}

void ModDllLoader::MarkFailed(OwnerId owner, const std::string& reason)
{
	for (auto& mod : _mods)
	{
		if (mod->owner == owner && mod->state == ModDllState::Loaded)
		{
			mod->state = ModDllState::Failed;
			mod->error = reason;
		}
	}
}

const ModDll* ModDllLoader::Find(OwnerId owner) const
{
	const auto found = std::ranges::find_if(_mods, [owner](const auto& mod) { return mod->owner == owner; });
	return found != _mods.end() ? found->get() : nullptr;
}

} // namespace moa_mod::loader
