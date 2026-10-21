/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <memory>

#include <dxgi.h>
#include <moa/common/Result.h>

struct ImDrawData;

namespace moa_mod::overlay
{

/// What one frame of a renderer did.
enum class RenderResult
{
	Drawn,   ///< The menu bar is on the back buffer.
	Skipped, ///< Nothing was drawn in this frame (for example the GPU was late). The next frame tries again.
	Rebuild, ///< The swap chain changed (buffer count or format). Make a new renderer.
	Failed,  ///< A call failed. The menu bar stops.
};

/// Draws the Dear ImGui data on the back buffer of the swap chain of the game, with the graphics API of the game. The
/// render thread of the game calls every function, with the ImGui context current, just before `Present`.
class OverlayRenderer
{
public:
	virtual ~OverlayRenderer() = default;

	/// The name of the graphics API, for the log and the status.
	[[nodiscard]] virtual const char* Api() const = 0;
	/// Start a frame of the renderer backend of ImGui.
	virtual void NewFrame() = 0;
	/// Draw the frame on the current back buffer.
	[[nodiscard]] virtual RenderResult Render(IDXGISwapChain* swapChain, ImDrawData* drawData) = 0;
	/// Release each reference to the back buffers, before the game changes their size.
	virtual void ReleaseBackBuffers() = 0;
};

/// The name of the graphics API of the swap chain: "Direct3D 11", "Direct3D 12" or "an unknown API".
[[nodiscard]] const char* SwapChainApi(IDXGISwapChain* swapChain);

/// Make the renderer for the API of the swap chain: Direct3D 11 if the device of the swap chain is a Direct3D 11
/// device, Direct3D 12 if it is a Direct3D 12 command queue. Return an error for any other API (for example Vulkan,
/// which has no DXGI swap chain). The ImGui context must be current.
[[nodiscard]] moa::Result<std::unique_ptr<OverlayRenderer>> CreateRenderer(IDXGISwapChain* swapChain);

} // namespace moa_mod::overlay
