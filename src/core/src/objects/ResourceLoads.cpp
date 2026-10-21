/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <algorithm>
#include <format>

#include <moa_mod/objects/ResourceLoads.h>

namespace moa_mod::objects
{

std::vector<size_t> FindMethodEntries(std::span<const uintptr_t> words, uintptr_t code, uintptr_t method)
{
	std::vector<size_t> found;
	if (method == 0)
	{
		return found;
	}
	for (size_t index = 0; index + 1 < words.size(); ++index)
	{
		if (words[index] != 0 && (code == 0 || words[index] == code) && words[index + 1] == method)
		{
			found.push_back(index);
		}
	}
	return found;
}

std::string NormalResourcePath(std::string_view path)
{
	std::string normal(path);
	std::ranges::transform(normal, normal.begin(), [](char c) {
		if (c == '\\')
		{
			return '/';
		}
		return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
	});
	return normal;
}

bool LoadTrace::Add(std::string_view path, std::string_view type)
{
	++_loads;
	auto key = Line(path, type);
	if (const auto found = _counts.find(key); found != _counts.end())
	{
		++found->second;
		return false;
	}
	_counts.emplace(key, 1);
	_order.push_back(std::move(key));
	return true;
}

uint64_t LoadTrace::Count(std::string_view path, std::string_view type) const
{
	const auto found = _counts.find(Line(path, type));
	return found == _counts.end() ? 0 : found->second;
}

std::vector<std::pair<std::string, uint64_t>> LoadTrace::Entries() const
{
	std::vector<std::pair<std::string, uint64_t>> entries;
	entries.reserve(_order.size());
	for (const auto& key : _order)
	{
		entries.emplace_back(key, _counts.find(key)->second);
	}
	return entries;
}

std::string LoadTrace::Line(std::string_view path, std::string_view type)
{
	return std::format("{}\t{}", type, path);
}

} // namespace moa_mod::objects
