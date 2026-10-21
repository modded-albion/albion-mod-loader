/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "moa_mod/overlay/DxgiHooks.h"

#include <cstring>

#include <array>
#include <atomic>
#include <filesystem>
#include <format>
#include <mutex>
#include <span>
#include <string>
#include <utility>

#include <MinHook.h>
#include <d3d11.h>
#include <moa_mod/overlay/HookRules.h>
#include <spdlog/spdlog.h>
#include <windows.h>
#include <wrl/client.h>

namespace moa_mod::overlay
{

using Microsoft::WRL::ComPtr;

namespace
{
using PresentFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

/// The hooks of the process. A code pointer cannot carry data, so the hook functions find the hooks here.
std::atomic<DxgiHooks*> s_hooks = nullptr;
/// The trampolines to the original functions. The install sets each one time, before the hook is on the code.
std::atomic<PresentFunction> s_originalPresent = nullptr;
std::atomic<ResizeBuffersFunction> s_originalResizeBuffers = nullptr;
/// The depth of the hook calls on this thread.
thread_local HookDepth t_depth;

constexpr wchar_t k_WindowClass[] = L"MoaModOverlayProbe";
constexpr UINT k_ProbeSize = 64;

// Each hook uses only a few bytes of stack: the render thread of the game calls it in each frame. While the menu bar
// does not show, the listener only reads some flags.
HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* self, UINT syncInterval, UINT flags)
{
	const auto original = s_originalPresent.load(std::memory_order_acquire);
	const HookScope scope(t_depth);
	auto* hooks = s_hooks.load(std::memory_order_acquire);
	if (scope.LoopFound() && hooks != nullptr)
	{
		hooks->ReportLoop("Present");
	}
	if (!scope.Outer() || hooks == nullptr)
	{
		return original(self, syncInterval, flags);
	}
	hooks->Listener().BeforePresent(self, flags);
	const auto result = original(self, syncInterval, flags);
	hooks->Listener().AfterPresent(self, result);
	return result;
}

HRESULT STDMETHODCALLTYPE HookResizeBuffers(IDXGISwapChain* self, UINT count, UINT width, UINT height, DXGI_FORMAT format,
                                            UINT flags)
{
	const auto original = s_originalResizeBuffers.load(std::memory_order_acquire);
	const HookScope scope(t_depth);
	auto* hooks = s_hooks.load(std::memory_order_acquire);
	if (scope.LoopFound() && hooks != nullptr)
	{
		hooks->ReportLoop("ResizeBuffers");
	}
	if (scope.Outer() && hooks != nullptr)
	{
		hooks->Listener().BeforeResizeBuffers(self);
	}
	return original(self, count, width, height, format, flags);
}

/// The hook functions of the mod. The install never keeps one of them as an original function.
[[nodiscard]] std::array<uintptr_t, 2> OwnHooks()
{
	return {reinterpret_cast<uintptr_t>(&HookPresent), reinterpret_cast<uintptr_t>(&HookResizeBuffers)};
}

/// A hidden window for the swap chains of the probe. The destructor removes it.
class ProbeWindow
{
public:
	ProbeWindow()
	{
		WNDCLASSEXW windowClass {.cbSize = sizeof(WNDCLASSEXW),
		                         .lpfnWndProc = DefWindowProcW,
		                         .hInstance = GetModuleHandleW(nullptr),
		                         .lpszClassName = k_WindowClass};
		_class = RegisterClassExW(&windowClass);
		_window = CreateWindowExW(0, k_WindowClass, L"", WS_OVERLAPPEDWINDOW, 0, 0, k_ProbeSize, k_ProbeSize, nullptr, nullptr,
		                          windowClass.hInstance, nullptr);
	}
	~ProbeWindow()
	{
		if (_window != nullptr)
		{
			DestroyWindow(_window);
		}
		if (_class != 0)
		{
			UnregisterClassW(k_WindowClass, GetModuleHandleW(nullptr));
		}
	}
	ProbeWindow(const ProbeWindow&) = delete;
	ProbeWindow& operator=(const ProbeWindow&) = delete;

	[[nodiscard]] HWND Get() const { return _window; }

private:
	ATOM _class = 0;
	HWND _window = nullptr;
};

/// Find a function of a system module. The probe loads the module if the game did not load it, and keeps it: the
/// swap chains of the probe use it until they are released.
template <typename Function>
[[nodiscard]] Function FindFunction(const wchar_t* module, const char* name)
{
	const auto handle = LoadLibraryW(module);
	return handle == nullptr ? nullptr : reinterpret_cast<Function>(GetProcAddress(handle, name));
}

/// The module that holds `address`, or null if no module holds it (for example code that a program made at run time).
[[nodiscard]] HMODULE ModuleOf(const void* address)
{
	HMODULE module = nullptr;
	constexpr DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
	if (address == nullptr || !GetModuleHandleExW(flags, static_cast<LPCWSTR>(address), &module))
	{
		return nullptr;
	}
	return module;
}

/// The file name of a module, for the log.
[[nodiscard]] std::string ModuleName(HMODULE module)
{
	if (module == nullptr)
	{
		return "no module";
	}
	std::array<wchar_t, MAX_PATH> path {};
	const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
	if (length == 0 || length >= path.size())
	{
		return "a module with no name";
	}
	const auto name = std::filesystem::path(std::wstring(path.data(), length)).filename().wstring();
	std::string text;
	text.reserve(name.size());
	for (const auto character : name)
	{
		text.push_back(character < 0x80 ? static_cast<char>(character) : '?');
	}
	return text;
}

/// Write a step of the install to the log at once, so that the log tells the last step if the game stops.
template <typename... Args>
void LogStep(spdlog::format_string_t<Args...> format, Args&&... args)
{
	spdlog::info(format, std::forward<Args>(args)...);
	spdlog::default_logger()->flush();
}

/// Make a Direct3D 11 swap chain with the flip model, as the game uses. If the flip model fails, use the old model.
/// The probe makes it before any hook is on the code, so the Steam overlay sees the functions of DXGI only.
[[nodiscard]] moa::Result<ComPtr<IDXGISwapChain>> ProbeD3D11(HWND window)
{
	const auto create = FindFunction<PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN>(L"d3d11.dll", "D3D11CreateDeviceAndSwapChain");
	if (create == nullptr)
	{
		return moa::MakeError("d3d11.dll has no D3D11CreateDeviceAndSwapChain");
	}
	HRESULT result = E_FAIL;
	for (const auto effect : {DXGI_SWAP_EFFECT_FLIP_DISCARD, DXGI_SWAP_EFFECT_DISCARD})
	{
		for (const auto driver : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP})
		{
			DXGI_SWAP_CHAIN_DESC description {};
			description.BufferDesc = {.Width = k_ProbeSize, .Height = k_ProbeSize, .Format = DXGI_FORMAT_R8G8B8A8_UNORM};
			description.SampleDesc = {.Count = 1, .Quality = 0};
			description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			description.BufferCount = effect == DXGI_SWAP_EFFECT_FLIP_DISCARD ? 2 : 1;
			description.OutputWindow = window;
			description.Windowed = TRUE;
			description.SwapEffect = effect;
			ComPtr<IDXGISwapChain> swapChain;
			ComPtr<ID3D11Device> device;
			ComPtr<ID3D11DeviceContext> context;
			result = create(nullptr, driver, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &description, &swapChain, &device,
			                nullptr, &context);
			if (SUCCEEDED(result))
			{
				return swapChain;
			}
		}
	}
	return moa::MakeError("D3D11CreateDeviceAndSwapChain failed with 0x{:08X}", static_cast<uint32_t>(result));
}

/// The target of a jump at the start of a function, or null if the function does not start with a jump that a hook
/// library writes.
[[nodiscard]] const void* JumpTarget(const uint8_t* code)
{
	int32_t displacement = 0;
	if (code[0] == 0xE9)
	{
		std::memcpy(&displacement, code + 1, sizeof(displacement));
		return code + 5 + displacement;
	}
	if (code[0] == 0xFF && code[1] == 0x25)
	{
		std::memcpy(&displacement, code + 2, sizeof(displacement));
		const void* target = nullptr;
		std::memcpy(&target, code + 6 + displacement, sizeof(target));
		return target;
	}
	if (code[0] == 0x48 && code[1] == 0xB8 && code[10] == 0xFF && code[11] == 0xE0)
	{
		const void* target = nullptr;
		std::memcpy(&target, code + 2, sizeof(target));
		return target;
	}
	return nullptr;
}

[[nodiscard]] std::string HexBytes(std::span<const uint8_t> bytes)
{
	std::string text;
	for (const auto byte : bytes)
	{
		text += std::format("{}{:02X}", text.empty() ? "" : " ", byte);
	}
	return text;
}

/// Start MinHook one time in the process. MinHook stays until the process ends, so that each trampoline stays valid.
[[nodiscard]] MH_STATUS InitializeMinHook()
{
	static std::once_flag once;
	static MH_STATUS status = MH_UNKNOWN;
	std::call_once(once, [] { status = MH_Initialize(); });
	return status;
}

[[nodiscard]] const char* HookTargetErrorText(HookTargetError error)
{
	switch (error)
	{
	case HookTargetError::NoFunction:
		return "the slot is empty";
	case HookTargetError::OwnHook:
		return "the slot points to a hook of the mod";
	case HookTargetError::OutsideModule:
		return "the slot points out of the module of the vtable: another program changed it";
	case HookTargetError::None:
		break;
	}
	return "no error";
}
} // namespace

moa::Result<std::unique_ptr<CodeHook>> CodeHook::Install(void* target, void* detour)
{
	if (const auto status = InitializeMinHook(); status != MH_OK)
	{
		return moa::MakeError("MinHook did not start: {}", MH_StatusToString(status));
	}
	void* original = nullptr;
	if (const auto status = MH_CreateHook(target, detour, &original); status != MH_OK)
	{
		return moa::MakeError("MinHook cannot make the hook: {}", MH_StatusToString(status));
	}
	return std::unique_ptr<CodeHook>(new CodeHook(target, original));
}

moa::Result<void> CodeHook::Enable()
{
	if (const auto status = MH_EnableHook(_target); status != MH_OK)
	{
		return moa::MakeError("MinHook cannot put the jump into the code: {}", MH_StatusToString(status));
	}
	std::memcpy(_jump.data(), _target, _jump.size());
	_installed = true;
	return {};
}

bool CodeHook::Restore()
{
	if (!_installed)
	{
		return true;
	}
	// Remove the hook only if the first bytes are still our jump.
	if (std::memcmp(_target, _jump.data(), _jump.size()) != 0)
	{
		return false;
	}
	_installed = MH_DisableHook(_target) != MH_OK;
	return !_installed;
}

moa::Result<std::unique_ptr<DxgiHooks>> DxgiHooks::Install(SwapChainListener& listener)
{
	if (s_hooks.load() != nullptr || s_originalPresent.load() != nullptr)
	{
		return moa::MakeError("the DXGI hooks are already installed");
	}
	auto hooks = std::unique_ptr<DxgiHooks>(new DxgiHooks(listener));
	LogStep("menu bar: step 1: the probe makes a hidden window on thread {}", GetCurrentThreadId());
	const ProbeWindow window;
	if (window.Get() == nullptr)
	{
		return moa::MakeError("the probe could not make a window (error {})", GetLastError());
	}
	void** vtable = nullptr;
	{
		LogStep("menu bar: step 2: the probe makes a Direct3D 11 device and swap chain");
		auto probe = ProbeD3D11(window.Get());
		if (!probe)
		{
			return probe.GetError();
		}
		// The vtable is in DXGI and stays after the probe releases its swap chain.
		vtable = *reinterpret_cast<void***>(probe.Value().Get());
		LogStep("menu bar: step 3: the probe releases its Direct3D 11 objects");
	}
	// The hooks must find the hooks object before the first call through a hook.
	s_hooks = hooks.get();
	LogStep("menu bar: step 4: the mod puts its hooks on the code of Present and ResizeBuffers");
	hooks->HookFunctions(vtable);
	LogStep("menu bar: step 5: the probe removes its window");
	if (!hooks->_present)
	{
		s_hooks = nullptr;
		std::string notes;
		for (const auto& note : hooks->_notes)
		{
			notes += (notes.empty() ? "" : "; ") + note;
		}
		return moa::MakeError("no hook is on Present: {}", notes);
	}
	return hooks;
}

void DxgiHooks::HookFunctions(void** vtable)
{
	const auto vtableModule = ModuleOf(vtable);
	_module = ModuleName(vtableModule);
	const auto ownHooks = OwnHooks();
	struct Target
	{
		const char* name;
		Slot slot;
		void* detour;
		std::unique_ptr<CodeHook>* hook;
	};
	const std::array targets {
	    Target {"Present", k_Present, reinterpret_cast<void*>(&HookPresent), &_present},
	    Target {"ResizeBuffers", k_ResizeBuffers, reinterpret_cast<void*>(&HookResizeBuffers), &_resizeBuffers},
	};
	for (const auto& target : targets)
	{
		auto* function = std::atomic_ref<void*>(vtable[target.slot]).load();
		const auto functionModule = ModuleOf(function);
		const auto error = CheckHookTarget(reinterpret_cast<uintptr_t>(function), ownHooks,
		                                   vtableModule != nullptr && functionModule == vtableModule);
		LogStep("menu bar: the vtable is in {}; the slot of {} points to {}", _module, target.name, ModuleName(functionModule));
		if (error != HookTargetError::None)
		{
			_notes.push_back(std::format("no hook on {}: {}", target.name, HookTargetErrorText(error)));
			continue;
		}
		auto hook = CodeHook::Install(function, target.detour);
		if (!hook)
		{
			_notes.push_back(std::format("no hook on {}: {}", target.name, hook.GetError().message));
			continue;
		}
		// Set the original function before the jump goes into the code: the first call can come at once.
		if (target.slot == k_Present)
		{
			s_originalPresent = reinterpret_cast<PresentFunction>((*hook)->Original());
		}
		else
		{
			s_originalResizeBuffers = reinterpret_cast<ResizeBuffersFunction>((*hook)->Original());
		}
		Watch(std::format("the code of {} before the hook", target.name), function);
		if (auto enabled = (*hook)->Enable(); !enabled)
		{
			_notes.push_back(std::format("no hook on {}: {}", target.name, enabled.GetError().message));
			continue;
		}
		LogStep("menu bar: the hook is on the code of {} in {}", target.name, ModuleName(functionModule));
		*target.hook = std::move(*hook);
	}
	// From now on, compare the code with the bytes after the install.
	_watches.clear();
	for (const auto& target : targets)
	{
		if (*target.hook)
		{
			Watch(std::format("the code of {} in {}", target.name, _module), (*target.hook)->Target());
		}
		Watch(std::format("the hook of the mod on {}", target.name), target.detour);
	}
}

moa::Result<void> DxgiHooks::ProbeAgain()
{
	const ProbeWindow window;
	if (window.Get() == nullptr)
	{
		return moa::MakeError("the probe could not make a window (error {})", GetLastError());
	}
	auto probe = ProbeD3D11(window.Get());
	if (!probe)
	{
		return probe.GetError();
	}
	LogStep("menu bar: the inspector made one more probe swap chain while the hooks are on the code");
	return {};
}

DxgiHooks::~DxgiHooks()
{
	Restore();
	DxgiHooks* self = this;
	s_hooks.compare_exchange_strong(self, nullptr);
}

void DxgiHooks::Restore()
{
	for (auto* hook : {_present.get(), _resizeBuffers.get()})
	{
		if (hook != nullptr)
		{
			hook->Restore();
		}
	}
}

void DxgiHooks::ReportLoop(const char* hook) noexcept
{
	if (_loopReported.exchange(true))
	{
		return;
	}
	try
	{
		spdlog::critical("menu bar: the hook on {} called itself {} times on thread {}: another program makes a loop "
		                 "with the hooks of the mod",
		                 hook, k_HookLoopDepth, GetCurrentThreadId());
		spdlog::default_logger()->flush();
	}
	catch (...)
	{
		// The log failed. The crash dump tells the rest.
	}
}

void DxgiHooks::Watch(std::string name, const void* code)
{
	CodeWatch watch {.name = std::move(name), .code = static_cast<const uint8_t*>(code)};
	std::memcpy(watch.bytes.data(), watch.code, watch.bytes.size());
	watch.logged = watch.bytes;
	const auto* target = JumpTarget(watch.bytes.data());
	LogStep("menu bar: {} starts with {} (a jump to {})", watch.name, HexBytes(watch.bytes),
	        target == nullptr ? std::string("no module") : ModuleName(ModuleOf(target)));
	_watches.push_back(std::move(watch));
}

void DxgiHooks::CheckCode()
{
	for (auto& watch : _watches)
	{
		std::array<uint8_t, 16> now {};
		std::memcpy(now.data(), watch.code, now.size());
		if (now == watch.logged)
		{
			continue;
		}
		watch.logged = now;
		const auto* target = JumpTarget(now.data());
		// Steam and other overlays put their own hooks on this code. The first change is a warning; a later change
		// of the same code goes only to the debug log.
		spdlog::log(std::exchange(watch.warned, true) ? spdlog::level::debug : spdlog::level::warn,
		            "menu bar: another program changed {}: it starts with {} (at the install: {}); the jump goes to {}",
		            watch.name, HexBytes(now), HexBytes(watch.bytes),
		            target == nullptr ? std::string("no jump") : ModuleName(ModuleOf(target)));
		spdlog::default_logger()->flush();
	}
}

nlohmann::json DxgiHooks::Describe() const
{
	return {
	    {"module", _module},
	    {"present", _present && _present->Installed()},
	    {"resizeBuffers", _resizeBuffers && _resizeBuffers->Installed()},
	    {"notes", _notes},
	};
}

} // namespace moa_mod::overlay
