/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "moa_mod/ResourceLoadSlot.h"

#include <cstring>

#include <algorithm>
#include <array>
#include <span>

#include <moa_mod/objects/ResourceLoads.h>
#include <windows.h>

namespace moa_mod
{

namespace
{
/// The bytes of the class that the mod examines for the entry of the method. The fixed part of a class and its table
/// of virtual methods are much smaller.
constexpr size_t k_ClassScanBytes = 0x1000;

/// Copy the words at `address` into `words`. Return false if the memory cannot be read.
bool CopyWords(const void* address, uintptr_t* words, size_t count)
{
	__try
	{
		std::memcpy(words, address, count * sizeof(uintptr_t));
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

/// Return the module that holds the code.
[[nodiscard]] HMODULE ModuleOf(const void* code)
{
	MEMORY_BASIC_INFORMATION information {};
	return VirtualQuery(code, &information, sizeof(information)) != 0 ? static_cast<HMODULE>(information.AllocationBase)
	                                                                  : nullptr;
}
} // namespace

size_t ReadableBytes(const void* address)
{
	MEMORY_BASIC_INFORMATION information {};
	if (VirtualQuery(address, &information, sizeof(information)) == 0 || information.State != MEM_COMMIT ||
	    (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
	{
		return 0;
	}
	const auto end = static_cast<const std::byte*>(information.BaseAddress) + information.RegionSize;
	return static_cast<size_t>(end - static_cast<const std::byte*>(address));
}

bool IsWritableMemory(const void* address, size_t size)
{
	MEMORY_BASIC_INFORMATION information {};
	if (VirtualQuery(address, &information, sizeof(information)) == 0 || information.State != MEM_COMMIT)
	{
		return false;
	}
	constexpr DWORD writable = PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
	const auto end = static_cast<const std::byte*>(information.BaseAddress) + information.RegionSize;
	return (information.Protect & writable) != 0 && (information.Protect & PAGE_GUARD) == 0 &&
	       static_cast<size_t>(end - static_cast<const std::byte*>(address)) >= size;
}

moa::Result<ResourceLoadSlot> FindResourceLoadSlot(const Managed& managed, Il2CppClass* resourcesApi,
                                                   const MethodInfo* activeApi, const MethodInfo* load, bool allowThisModule)
{
	// The object that `Resources.Load` calls. Its class has the table that the call reads, also if the game puts its
	// own loader in place of the loader of Unity.
	auto active = managed.Invoke(activeApi, nullptr);
	if (!active)
	{
		return active.GetError();
	}
	if (*active == nullptr)
	{
		return moa::MakeError("Unity has no resource loader now");
	}
	auto* klass = managed.il2cpp_object_get_class(*active);
	const auto* method = managed.il2cpp_object_get_virtual_method(*active, load);
	if (klass == nullptr || method == nullptr)
	{
		return moa::MakeError("the resource loader has no method Load");
	}
	ResourceLoadSlot result {.className = managed.Api().FullName(klass), .unityLoader = klass == resourcesApi};
	std::array<uintptr_t, k_ClassScanBytes / sizeof(uintptr_t)> words {};
	const auto count = std::min(words.size(), ReadableBytes(klass) / sizeof(uintptr_t));
	if (count == 0 || !CopyWords(klass, words.data(), count))
	{
		return moa::MakeError("the mod cannot read the class {}", result.className);
	}
	const auto entries = objects::FindMethodEntries(std::span(words).first(count), 0, reinterpret_cast<uintptr_t>(method));
	if (entries.size() != 1)
	{
		return moa::MakeError("the class {} has {} entries for Load, not 1", result.className, entries.size());
	}
	result.slot = reinterpret_cast<void**>(reinterpret_cast<uintptr_t*>(klass) + entries.front());
	result.code = reinterpret_cast<void*>(words[entries.front()]);
	const auto* owner = ModuleOf(result.code);
	result.gameCode = owner != nullptr && owner == GetModuleHandleW(L"GameAssembly.dll");
	const bool thisModule = owner != nullptr && owner == ModuleOf(reinterpret_cast<const void*>(&FindResourceLoadSlot));
	if (!result.gameCode && !(allowThisModule && thisModule))
	{
		return moa::MakeError("the entry for Load in the class {} does not point to the code of the game", result.className);
	}
	if (!IsWritableMemory(result.slot, sizeof(void*)))
	{
		return moa::MakeError("the entry for Load in the class {} is not in writable memory", result.className);
	}
	result.methodCode = result.code == *reinterpret_cast<void* const*>(method);
	return result;
}

} // namespace moa_mod
