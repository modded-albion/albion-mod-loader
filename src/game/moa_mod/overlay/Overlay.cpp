/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "moa_mod/overlay/Overlay.h"

#include <chrono>
#include <exception>
#include <format>

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <spdlog/spdlog.h>

#include "moa_mod/CrashDump.h"
#include "moa_mod/overlay/OverlayRenderer.h"
#include "moa_mod/overlay/OverlayUi.h"

// The Win32 backend of ImGui declares its window procedure handler only for code that asks for it.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace moa_mod::overlay
{

namespace
{
/// The menu bar of the process. The window procedure is a plain function, so it finds the menu bar here.
std::atomic<Overlay*> s_overlay = nullptr;
/// The window procedure of the game, which the menu bar calls for each message.
std::atomic<WNDPROC> s_gameWindowProc = nullptr;

/// The longest time that a thread waits for the ImGui context. A longer wait skips the work, so that no thread of the
/// game stops.
constexpr auto k_LockTimeout = std::chrono::milliseconds(100);
/// The main frames with no `Present` after which the log tells that the hooks see no frame.
constexpr uint64_t k_NoPresentFrames = 3600;
/// The main frames between two checks of the code of the hooks.
constexpr uint64_t k_CodeCheckFrames = 30;

/// Call the function, and return false if it causes an access violation. The stack then unwinds without destructors,
/// so the menu bar stops at once after a false result.
bool CallWithGuard(void (*function)(void*), void* context)
{
	__try
	{
		function(context);
		return true;
	}
	__except (FilterAccessViolation(GetExceptionInformation()))
	{
		return false;
	}
}

LRESULT CALLBACK OverlayWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (auto* overlay = s_overlay.load(); overlay != nullptr)
	{
		if (overlay->OnWindowMessage(window, message, wParam, lParam).swallow)
		{
			// Raw input needs the default procedure, which releases the data of the message.
			return message == WM_INPUT ? DefWindowProcW(window, message, wParam, lParam) : 0;
		}
	}
	const auto game = s_gameWindowProc.load();
	return game != nullptr ? CallWindowProcW(game, window, message, wParam, lParam)
	                       : DefWindowProcW(window, message, wParam, lParam);
}

/// Return true if the system cursor shows. The game hides it in the town and draws its own cursor (the hand). Only
/// `Describe` uses it, for the checks; the bar never changes the cursor.
[[nodiscard]] bool SystemCursorShown()
{
	CURSORINFO info {.cbSize = sizeof(CURSORINFO)};
	return GetCursorInfo(&info) && (info.flags & CURSOR_SHOWING) != 0 && info.hCursor != nullptr;
}

/// Return true if the window is a top-level window of this process.
[[nodiscard]] bool IsGameWindow(HWND window)
{
	DWORD process = 0;
	GetWindowThreadProcessId(window, &process);
	return window != nullptr && process == GetCurrentProcessId() && GetAncestor(window, GA_ROOT) == window;
}
} // namespace

moa::Result<std::unique_ptr<Overlay>> Overlay::Install(OverlayHost host)
{
	if (s_overlay.load() != nullptr)
	{
		return moa::MakeError("the menu bar is already installed");
	}
	// The modules tell which graphics API the game started. The hook on the swap chain then tells it for sure.
	const auto loaded = [](const wchar_t* name) { return GetModuleHandleW(name) != nullptr; };
	spdlog::info("menu bar: the game loaded d3d11.dll {}, d3d12.dll {}, vulkan-1.dll {}", loaded(L"d3d11.dll"),
	             loaded(L"d3d12.dll"), loaded(L"vulkan-1.dll"));
	auto overlay = std::unique_ptr<Overlay>(new Overlay(std::move(host)));
	auto hooks = DxgiHooks::Install(*overlay);
	if (!hooks)
	{
		return hooks.GetError();
	}
	overlay->_hooks = std::move(*hooks);
	s_overlay = overlay.get();
	return overlay;
}

Overlay::Overlay(OverlayHost host)
    : _host(std::move(host))
{
}

Overlay::~Overlay()
{
	Shutdown();
}

void Overlay::Shutdown()
{
	_failed = true;
	_visible = false;
	RestoreWindow();
	if (_hooks)
	{
		_hooks->Restore();
	}
	Overlay* self = this;
	s_overlay.compare_exchange_strong(self, nullptr);
}

void Overlay::Fail(const std::string& reason) noexcept
{
	if (_failed.exchange(true))
	{
		return;
	}
	_visible = false;
	try
	{
		spdlog::error("menu bar: {}; the menu bar stops, the game continues", reason);
		SetErrorText(reason);
	}
	catch (...)
	{
		// The log failed. The menu bar stops all the same.
	}
}

void Overlay::SetErrorText(const std::string& error)
{
	const std::scoped_lock lock(_statusMutex);
	_lastError = error;
}

VisibilityChange Overlay::Tick(bool menuOpen, bool togglePin)
{
	_visibility.SetEnabled(!_failed.load());
	const auto change = _visibility.Update(menuOpen, togglePin);
	_pinned = _visibility.Pinned();
	_visible = change.visible;
	if (change.shown || change.hidden)
	{
		spdlog::info("menu bar: {} (game menu {}, pinned {})", change.shown ? "shows" : "hides", menuOpen,
		             _visibility.Pinned());
		// The keys and the buttons that ImGui holds go up, so that no control stays active while the bar hides.
		if (std::unique_lock lock(_mutex, k_LockTimeout); lock.owns_lock() && _context != nullptr && !_failed.load())
		{
			ImGui::SetCurrentContext(_context);
			ImGui::GetIO().AddFocusEvent(change.shown && GetForegroundWindow() == _window);
		}
	}
	const auto frames = ++_mainFrames;
	if (_hooks && frames % k_CodeCheckFrames == 0)
	{
		_hooks->CheckCode();
	}
	if (frames >= k_NoPresentFrames && _presentCalls.load() == 0 && !_noFramesLogged)
	{
		_noFramesLogged = true;
		spdlog::warn("menu bar: no Present call came through the hooks in {} frames. The game uses another swap chain "
		             "class or another graphics API; the menu bar cannot show",
		             frames);
		SetErrorText("no Present call came through the hooks");
	}
	return change;
}

void Overlay::Publish(OverlaySnapshot snapshot)
{
	const std::scoped_lock lock(_snapshotMutex);
	_snapshot = std::move(snapshot);
	++_snapshotRevision;
}

nlohmann::json Overlay::Describe() const
{
	const std::scoped_lock lock(_statusMutex);
	return {
	    {"api", _api.empty() ? "not known yet" : _api},
	    {"visible", _visible.load()},
	    {"pinned", _pinned.load()},
	    {"failed", _failed.load()},
	    {"presentCalls", _presentCalls.load()},
	    {"framesDrawn", _framesDrawn.load()},
	    {"lastError", _lastError},
	    {"systemCursorShown", SystemCursorShown()},
	    {"hooks", _hooks ? _hooks->Describe() : nlohmann::json()},
	};
}

InputDecision Overlay::OnWindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
	if (_failed.load() || !_visible.load())
	{
		return {};
	}
	struct Call
	{
		Overlay* self;
		HWND window;
		UINT message;
		WPARAM wParam;
		LPARAM lParam;
		InputDecision decision;
	} call {this, window, message, wParam, lParam, {}};
	const auto work = [](void* context) {
		auto& c = *static_cast<Call*>(context);
		std::unique_lock lock(c.self->_mutex, k_LockTimeout);
		if (!lock.owns_lock() || c.self->_context == nullptr || c.window != c.self->_window)
		{
			return;
		}
		ImGui::SetCurrentContext(c.self->_context);
		const auto& io = ImGui::GetIO();
		c.decision = DecideInput(ClassifyMessage(c.message), c.wParam, true, io.WantCaptureMouse, io.WantCaptureKeyboard);
		if (c.decision.feedOverlay)
		{
			ImGui_ImplWin32_WndProcHandler(c.window, c.message, c.wParam, c.lParam);
		}
	};
	if (!CallWithGuard(work, &call))
	{
		Fail("an access violation in the window procedure");
		return {};
	}
	return call.decision;
}

void Overlay::BeforePresent(IDXGISwapChain* swapChain, UINT flags) noexcept
{
	// While the bar does not show, the hook only counts the call and reads two flags. The bar does nothing with the
	// device, the window or ImGui before it shows the first time.
	if (_presentCalls.fetch_add(1, std::memory_order_relaxed) == 0)
	{
		LogFirstPresent(swapChain);
	}
	if (!_visible.load(std::memory_order_relaxed) || _failed.load(std::memory_order_relaxed) ||
	    (flags & DXGI_PRESENT_TEST) != 0)
	{
		return;
	}
	struct Call
	{
		Overlay* self;
		IDXGISwapChain* swapChain;
	} call {this, swapChain};
	const auto work = [](void* context) {
		auto& c = *static_cast<Call*>(context);
		try
		{
			c.self->Present(c.swapChain);
		}
		catch (const std::exception& error)
		{
			c.self->Fail(error.what());
		}
		catch (...)
		{
			c.self->Fail("an unknown exception");
		}
	};
	if (!CallWithGuard(work, &call))
	{
		Fail("an access violation in the code that draws");
	}
}

void Overlay::LogFirstPresent(IDXGISwapChain* swapChain) noexcept
{
	const auto work = [](void* context) {
		auto* swapChain = static_cast<IDXGISwapChain*>(context);
		try
		{
			spdlog::info("menu bar: the first Present came through the hook on thread {}; the swap chain uses {}",
			             GetCurrentThreadId(), SwapChainApi(swapChain));
		}
		catch (...)
		{
			// The log failed. The bar continues.
		}
	};
	if (!CallWithGuard(work, swapChain))
	{
		Fail("an access violation when the bar read the first swap chain");
	}
}

void Overlay::AfterPresent(IDXGISwapChain* swapChain, HRESULT result) noexcept
{
	if (swapChain == _swapChain && (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET))
	{
		// The renderer starts again on the new device at the next frame.
		_rebuild = true;
	}
}

void Overlay::BeforeResizeBuffers(IDXGISwapChain* swapChain) noexcept
{
	if (_failed.load() || swapChain != _swapChain)
	{
		return;
	}
	const auto work = [](void* context) {
		auto* self = static_cast<Overlay*>(context);
		std::unique_lock lock(self->_mutex, k_LockTimeout);
		if (!lock.owns_lock())
		{
			// The renderer cannot release the buffers now, so it starts again at the next frame.
			self->_rebuild = true;
			return;
		}
		if (self->_renderer)
		{
			ImGui::SetCurrentContext(self->_context);
			self->_renderer->ReleaseBackBuffers();
		}
	};
	if (!CallWithGuard(work, this))
	{
		Fail("an access violation before ResizeBuffers");
	}
}

bool Overlay::Bind(IDXGISwapChain* swapChain)
{
	if (swapChain == _swapChain && !_rebuild.load())
	{
		return _renderer != nullptr;
	}
	DXGI_SWAP_CHAIN_DESC description {};
	if (FAILED(swapChain->GetDesc(&description)) || !IsGameWindow(description.OutputWindow))
	{
		return false;
	}
	// A swap chain of another window of the game does not draw the bar.
	if (_window != nullptr && description.OutputWindow != _window && swapChain != _swapChain)
	{
		return false;
	}
	std::unique_lock lock(_mutex, k_LockTimeout);
	if (!lock.owns_lock())
	{
		return false;
	}
	if (_context == nullptr)
	{
		spdlog::info("menu bar: bind step 1: the bar makes the ImGui context on thread {}", GetCurrentThreadId());
		_context = ImGui::CreateContext();
		ImGui::SetCurrentContext(_context);
		auto& io = ImGui::GetIO();
		// The menu bar writes no file: no imgui.ini and no log, in the game folder or anywhere else.
		io.IniFilename = nullptr;
		io.LogFilename = nullptr;
		// The bar never changes or draws a cursor. The game shows its own cursor (the hand, or the cursor of its menu),
		// and that cursor is the only cursor on the screen.
		io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
		io.MouseDrawCursor = false;
		ImGui::StyleColorsDark();
		spdlog::info("menu bar: bind step 2: the Win32 backend of ImGui starts");
		if (!ImGui_ImplWin32_Init(description.OutputWindow))
		{
			throw std::runtime_error("the Win32 backend of ImGui did not start");
		}
		_window = description.OutputWindow;
		spdlog::info("menu bar: bind step 3: the bar puts its window procedure on the game window");
		SubclassWindow(_window);
	}
	ImGui::SetCurrentContext(_context);
	_renderer.reset();
	spdlog::info("menu bar: bind step 4: the bar makes its renderer on the device of the game");
	auto renderer = CreateRenderer(swapChain);
	if (!renderer)
	{
		throw std::runtime_error(renderer.GetError().message + "; the game uses an API that the menu bar does not know");
	}
	_renderer = std::move(*renderer);
	_swapChain = swapChain;
	_rebuild = false;
	{
		const std::scoped_lock status(_statusMutex);
		_api = _renderer->Api();
	}
	spdlog::info("menu bar: draws with {} on the swap chain of the game window ({}x{})", _renderer->Api(),
	             description.BufferDesc.Width, description.BufferDesc.Height);
	return true;
}

void Overlay::Present(IDXGISwapChain* swapChain)
{
	if (!Bind(swapChain))
	{
		return;
	}
	std::unique_lock lock(_mutex, k_LockTimeout);
	if (lock.owns_lock())
	{
		DrawFrame(swapChain);
	}
}

void Overlay::DrawFrame(IDXGISwapChain* swapChain)
{
	ImGui::SetCurrentContext(_context);
	{
		const std::scoped_lock lock(_snapshotMutex);
		if (_snapshotRevision != _shownRevision)
		{
			_shown = _snapshot;
			_shownRevision = _snapshotRevision;
		}
	}
	_renderer->NewFrame();
	ImGui_ImplWin32_NewFrame();
	auto& style = ImGui::GetStyle();
	style.FontScaleMain = static_cast<float>(_shown.requested.overlay.fontScale);
	style.FontScaleDpi = ImGui_ImplWin32_GetDpiScaleForHwnd(_window);
	ImGui::NewFrame();
	// The controls show the settings of the mods, except while the user changes a control or a change waits for
	// the main thread.
	if (!ImGui::IsAnyItemActive() && _pendingEdits.load() == 0)
	{
		_draft.clear();
		for (const auto& item : _shown.loaderItems)
		{
			_draft[item.id] = item.state.value;
		}
		for (const auto& menu : _shown.menus)
		{
			for (const auto& item : menu.items)
			{
				_draft[item.id] = item.state.value;
			}
		}
		_draftLoader = _shown.requested;
	}
	const auto status = Describe();
	const std::function<void(OverlayCommand)> post = [this](OverlayCommand command) { Post(std::move(command)); };
	DrawOverlayMenuBar(OverlayUiFrame {
	    .snapshot = _shown, .draft = _draft, .draftLoader = _draftLoader, .post = post, .overlayStatus = status});
	ImGui::Render();
	switch (_renderer->Render(swapChain, ImGui::GetDrawData()))
	{
	case RenderResult::Drawn:
		++_framesDrawn;
		break;
	case RenderResult::Skipped:
		break;
	case RenderResult::Rebuild:
		_rebuild = true;
		break;
	case RenderResult::Failed:
		throw std::runtime_error(std::format("the {} renderer failed", _renderer->Api()));
	}
}

void Overlay::Post(OverlayCommand command)
{
	++_pendingEdits;
	_host.runOnMainThread([this, command = std::move(command)] {
		try
		{
			_host.apply(command);
		}
		catch (const std::exception& error)
		{
			spdlog::error("menu bar: the change failed: {}", error.what());
			SetErrorText(error.what());
		}
		--_pendingEdits;
	});
}

void Overlay::SubclassWindow(HWND window)
{
	const auto previous = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
	if (previous == nullptr || previous == &OverlayWindowProc)
	{
		throw std::runtime_error("the menu bar cannot read the window procedure of the game");
	}
	s_gameWindowProc = previous;
	::SetLastError(0);
	if (SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&OverlayWindowProc)) == 0 && GetLastError() != 0)
	{
		throw std::runtime_error(std::format("SetWindowLongPtrW failed with error {}", GetLastError()));
	}
	spdlog::info("menu bar: the window procedure of the game window has the input handler of the bar");
}

void Overlay::RestoreWindow()
{
	const auto game = s_gameWindowProc.load();
	if (_window == nullptr || game == nullptr)
	{
		return;
	}
	// Put the procedure of the game back only if no other program changed it after the bar.
	if (reinterpret_cast<WNDPROC>(GetWindowLongPtrW(_window, GWLP_WNDPROC)) == &OverlayWindowProc)
	{
		SetWindowLongPtrW(_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(game));
	}
}

} // namespace moa_mod::overlay
