/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace moa_mod::objects
{

/// Find the entries of a table of method pointers that hold the pair (`code`, `method`): `words[i]` is `code` and
/// `words[i + 1]` is `method`. A `code` of 0 accepts each code that is not 0. Return the index of the first word of
/// each pair. The mod uses it to find the entry of a virtual method in the memory of its class, by the method and its
/// code. It does not use a fixed position.
[[nodiscard]] std::vector<size_t> FindMethodEntries(std::span<const uintptr_t> words, uintptr_t code, uintptr_t method);

/// Return the resource path in a form that compares as the game compares paths: lower case, with forward slashes.
[[nodiscard]] std::string NormalResourcePath(std::string_view path);

/// The resource paths and types that the game loads, each one time, in the order of the first load.
class LoadTrace
{
public:
	/// Count one load. Return true if it is the first load of this path with this type.
	bool Add(std::string_view path, std::string_view type);

	/// The count of different paths and types.
	[[nodiscard]] size_t Size() const { return _order.size(); }

	/// The loads of all paths and types.
	[[nodiscard]] uint64_t Loads() const { return _loads; }

	/// The count of loads of one path and type.
	[[nodiscard]] uint64_t Count(std::string_view path, std::string_view type) const;

	/// The paths and types with their counts, in the order of the first load.
	[[nodiscard]] std::vector<std::pair<std::string, uint64_t>> Entries() const;

	/// The line of the trace file for a load: the type, a tab and the path.
	[[nodiscard]] static std::string Line(std::string_view path, std::string_view type);

private:
	std::map<std::string, uint64_t, std::less<>> _counts;
	std::vector<std::string> _order;
	uint64_t _loads = 0;
};

} // namespace moa_mod::objects
