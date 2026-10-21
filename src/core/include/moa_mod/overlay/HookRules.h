/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <algorithm>
#include <span>

namespace moa_mod::overlay
{

/// Why the menu bar does not put a hook on a function of the swap chain.
enum class HookTargetError
{
	None,          ///< The function is correct for a hook.
	NoFunction,    ///< The slot of the vtable is empty.
	OwnHook,       ///< The slot points to a hook of the mod. The original function is then not known.
	OutsideModule, ///< The slot points out of the module of the vtable: another program changed the slot.
};

/// Check the function that a slot of a swap chain vtable points to, before the mod puts its hook on the code of that
/// function. The mod reads the function one time only, and never keeps a hook of its own as the original function:
/// that hook would call itself with no end. `ownHooks` are the hook functions of the mod.
[[nodiscard]] constexpr HookTargetError CheckHookTarget(uintptr_t function, std::span<const uintptr_t> ownHooks,
                                                        bool inVtableModule)
{
	if (function == 0)
	{
		return HookTargetError::NoFunction;
	}
	if (std::ranges::find(ownHooks, function) != ownHooks.end())
	{
		return HookTargetError::OwnHook;
	}
	if (!inVtableModule)
	{
		return HookTargetError::OutsideModule;
	}
	return HookTargetError::None;
}

/// The depth of the hook calls at which the mod tells of a loop. A correct chain of hooks calls a hook of the mod one
/// time, or two times if one function of DXGI calls another.
inline constexpr int k_HookLoopDepth = 8;

/// The depth of the hook calls on one thread. Each hook enters it at the start and leaves it at the end. Only the
/// outer call gives the frame to the menu bar. An inner call calls the original function at once and does no other
/// work.
class HookDepth
{
public:
	/// What a hook does in this call.
	struct Entry
	{
		bool outer = false;     ///< No other hook of the mod is in progress on this thread.
		bool loopFound = false; ///< The depth is at `k_HookLoopDepth` for the first time on this thread.
	};

	[[nodiscard]] constexpr Entry Enter()
	{
		++_depth;
		const bool loop = _depth >= k_HookLoopDepth && !_loopReported;
		_loopReported = _loopReported || loop;
		return {.outer = _depth == 1, .loopFound = loop};
	}

	constexpr void Leave()
	{
		if (_depth > 0)
		{
			--_depth;
		}
	}

	[[nodiscard]] constexpr int Depth() const { return _depth; }

private:
	int _depth = 0;
	bool _loopReported = false;
};

/// Enter a `HookDepth` for the time of one hook call.
class HookScope
{
public:
	explicit HookScope(HookDepth& depth)
	    : _depth(depth)
	    , _entry(depth.Enter())
	{
	}
	~HookScope() { _depth.Leave(); }
	HookScope(const HookScope&) = delete;
	HookScope& operator=(const HookScope&) = delete;

	[[nodiscard]] bool Outer() const { return _entry.outer; }
	[[nodiscard]] bool LoopFound() const { return _entry.loopFound; }

private:
	HookDepth& _depth;
	HookDepth::Entry _entry;
};

} // namespace moa_mod::overlay
