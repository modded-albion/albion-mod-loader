/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstddef>

#include <string>

#include <moa/common/Result.h>

#include "moa_mod/Managed.h"

namespace moa_mod
{

/// The entry of `ResourcesAPI.Load(string, Type)` in the table of virtual methods of the resource loader that
/// `Resources.Load` calls. A mod puts its own function into the entry to see each load of the game.
struct ResourceLoadSlot
{
	void** slot = nullptr;    ///< The entry. It holds a code pointer.
	void* code = nullptr;     ///< The code that the entry holds now: the game, or a hook of this DLL.
	std::string className;    ///< The class of the resource loader.
	bool unityLoader = false; ///< The loader is the loader of Unity, not a loader of the game.
	bool gameCode = false;    ///< The entry holds code of GameAssembly.dll.
	bool methodCode = false;  ///< The entry holds the code pointer of the method itself.
};

/// Find the entry of `load` (the virtual method `ResourcesAPI.Load`) in the class of the object that `activeApi`
/// (`ResourcesAPI.ActiveAPI`) gives. The function finds the entry by the method and its code, not at a fixed
/// position. With `allowThisModule`, the entry can hold a function of this DLL (the hook of another mod), else it must
/// hold code of the game. Return an error if the entry is not found exactly one time, holds other code, or is not in
/// writable memory. Call it on a thread that IL2CPP attached.
[[nodiscard]] moa::Result<ResourceLoadSlot> FindResourceLoadSlot(const Managed& managed, Il2CppClass* resourcesApi,
                                                                 const MethodInfo* activeApi, const MethodInfo* load,
                                                                 bool allowThisModule);

/// Return the count of bytes from `address` to the end of its memory region, or 0 if the memory cannot be read.
[[nodiscard]] size_t ReadableBytes(const void* address);

/// Return true if the process can write `size` bytes at `address`, in one memory region.
[[nodiscard]] bool IsWritableMemory(const void* address, size_t size);

} // namespace moa_mod
