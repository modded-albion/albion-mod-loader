/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "moa_mod/objects/KnackMerge.h"

#include <charconv>
#include <cmath>

#include <algorithm>
#include <array>
#include <format>
#include <optional>

#include <nlohmann/json.hpp>

#include "moa_mod/objects/Pack.h"

namespace moa_mod::objects
{
namespace
{

using OrderedJson = nlohmann::ordered_json;
using Fields = std::vector<std::pair<std::string, std::string>>;

constexpr std::string_view k_ByteOrderMark = "\xEF\xBB\xBF";

[[nodiscard]] std::string Quote(std::string_view text)
{
	return OrderedJson(std::string(text)).dump(-1, ' ', false, OrderedJson::error_handler_t::replace);
}

/// The shortest text that reads back as the same double.
[[nodiscard]] std::string Shortest(double value)
{
	std::array<char, 64> buffer {};
	const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
	if (error != std::errc {})
	{
		return "0";
	}
	return std::string(buffer.data(), end);
}

[[nodiscard]] std::optional<double> ParseDouble(std::string_view text)
{
	double value = 0.0;
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (error != std::errc {} || end != text.data() + text.size())
	{
		return std::nullopt;
	}
	return value;
}

/// A text with a number of decimals, or the shortest text when that number of decimals changes the value.
[[nodiscard]] std::string Decimals(double value, size_t decimals)
{
	auto text = std::format("{:.{}f}", value, decimals);
	if (const auto back = ParseDouble(text); back && *back == value)
	{
		return text;
	}
	return Shortest(value);
}

/// Return true for a text such as "25.00" or "-0.5": digits, one point, digits.
[[nodiscard]] bool IsDecimalText(std::string_view text)
{
	if (text.starts_with('-'))
	{
		text.remove_prefix(1);
	}
	const auto point = text.find('.');
	if (point == std::string_view::npos || point == 0 || point + 1 == text.size())
	{
		return false;
	}
	const auto digits = [](std::string_view part) {
		return std::ranges::all_of(part, [](char c) { return c >= '0' && c <= '9'; });
	};
	return digits(text.substr(0, point)) && digits(text.substr(point + 1));
}

[[nodiscard]] std::string FormatNumber(double value, const OrderedJson& form)
{
	if (form.is_string())
	{
		const auto text = form.get<std::string>();
		if (text.empty())
		{
			return Quote(Decimals(value, 2));
		}
		// Keep the count of decimals of the template, for example "0.47000" for the attack of a block.
		return Quote(IsDecimalText(text) ? Decimals(value, text.size() - text.find('.') - 1) : Shortest(value));
	}
	if (form.is_boolean())
	{
		return value != 0.0 ? "true" : "false";
	}
	if (std::isfinite(value) && std::trunc(value) == value && std::abs(value) < 1.0e15)
	{
		return std::format("{}", static_cast<long long>(value));
	}
	return Shortest(value);
}

[[nodiscard]] std::string FormatBool(bool value, const OrderedJson& form)
{
	if (form.is_string())
	{
		return Quote(value ? "True" : "False");
	}
	if (form.is_number())
	{
		return value ? "1" : "0";
	}
	return value ? "true" : "false";
}

[[nodiscard]] Fields::iterator FindField(Fields& fields, std::string_view key)
{
	return std::ranges::find(fields, key, &std::pair<std::string, std::string>::first);
}

[[nodiscard]] std::string FieldValueText(const Fields& fields, std::string_view key)
{
	const auto found = std::ranges::find(fields, key, &std::pair<std::string, std::string>::first);
	if (found == fields.end())
	{
		return {};
	}
	const auto json = OrderedJson::parse(found->second, nullptr, false);
	if (json.is_string())
	{
		return json.get<std::string>();
	}
	return found->second;
}

[[nodiscard]] std::string SerialiseRow(const Fields& fields)
{
	std::string text = "{";
	for (size_t i = 0; i < fields.size(); ++i)
	{
		if (i > 0)
		{
			text += ',';
		}
		text += Quote(fields[i].first);
		text += ':';
		text += fields[i].second;
	}
	text += '}';
	return text;
}

/// A row of the table or a row that the merge added: its key, level and unique values.
struct KnownRow
{
	std::string pack; ///< Empty for a game row.
	std::string name;
	std::string level;
	Fields fields;
};

[[nodiscard]] bool SameKey(const KnackTableRules& rules, std::string_view a, std::string_view b)
{
	return rules.keyIgnoresCase ? EqualsIgnoreCase(a, b) : a == b;
}

[[nodiscard]] std::string Owner(const KnownRow& row)
{
	return row.pack.empty() ? std::format("the game row \"{}\"", row.name)
	                        : std::format("the row \"{}\" of the pack \"{}\"", row.name, row.pack);
}

/// Check one request and make its row. Return an error text if the pack cannot add the row.
[[nodiscard]] moa::Result<KnownRow> MakeRow(const KnackPackRows& pack, const KnackRowRequest& request,
                                            const std::vector<KnownRow>& gameRows, const std::vector<KnownRow>& added,
                                            const KnackTableRules& rules)
{
	if (request.name.empty())
	{
		return moa::MakeError("a row has no {}", rules.keyField);
	}
	const auto templateRow = std::ranges::find_if(gameRows, [&](const KnownRow& row) {
		return SameKey(rules, row.name, request.templateName) &&
		       (request.templateLevel.empty() || row.level == request.templateLevel);
	});
	if (templateRow == gameRows.end())
	{
		return moa::MakeError("{}: the template \"{}\" is not a row of the game", request.name, request.templateName);
	}

	KnownRow row {.pack = pack.pack, .name = request.name, .level = {}, .fields = templateRow->fields};
	if (auto key = FindField(row.fields, rules.keyField); key != row.fields.end())
	{
		key->second = Quote(request.name);
	}
	else
	{
		row.fields.emplace_back(rules.keyField, Quote(request.name));
	}
	for (const auto& [key, value] : request.values)
	{
		auto field = FindField(row.fields, key);
		const auto form =
		    field == row.fields.end() ? OrderedJson(std::string {}) : OrderedJson::parse(field->second, nullptr, false);
		auto text = std::visit(
		    [&](const auto& item) -> std::string {
			    using T = std::decay_t<decltype(item)>;
			    if constexpr (std::is_same_v<T, std::string>)
			    {
				    return Quote(item);
			    }
			    else if constexpr (std::is_same_v<T, bool>)
			    {
				    return FormatBool(item, form);
			    }
			    else
			    {
				    return FormatNumber(item, form);
			    }
		    },
		    value);
		if (field == row.fields.end())
		{
			row.fields.emplace_back(key, std::move(text));
		}
		else
		{
			field->second = std::move(text);
		}
	}
	if (!rules.levelField.empty())
	{
		row.level = FieldValueText(row.fields, rules.levelField);
	}

	const auto clash = [&](const KnownRow& other) -> std::optional<moa::Error> {
		if (SameKey(rules, other.name, row.name) && (rules.levelField.empty() || other.level == row.level))
		{
			if (rules.levelField.empty())
			{
				return moa::MakeError("{}: {} has the same {}", request.name, Owner(other), rules.keyField);
			}
			return moa::MakeError("{}: {} has the same {} and {} \"{}\"", request.name, Owner(other), rules.keyField,
			                      rules.levelField, row.level);
		}
		for (const auto& unique : request.uniqueFields)
		{
			const auto mine = FieldValueText(row.fields, unique);
			if (!mine.empty() && EqualsIgnoreCase(mine, FieldValueText(other.fields, unique)))
			{
				return moa::MakeError("{}: {} has the {} \"{}\"", request.name, Owner(other), unique, mine);
			}
		}
		return std::nullopt;
	};
	for (const auto& other : gameRows)
	{
		if (auto error = clash(other))
		{
			return *error;
		}
	}
	for (const auto& other : added)
	{
		if (auto error = clash(other))
		{
			return *error;
		}
	}
	return row;
}

/// The place of one value in the text of a table.
struct ValueSpan
{
	size_t begin = 0;
	size_t end = 0;
};

/// A reader of JSON text that gives the byte places of the values of the rows of a table. It reads only the form; the
/// values themselves are read by the JSON library.
class TableScanner
{
public:
	explicit TableScanner(std::string_view text)
	    : _text(text)
	{
	}

	/// The values of the fields of each row: the field name (with no escapes) and the place of its value.
	using RowSpans = std::vector<std::pair<std::string, ValueSpan>>;

	[[nodiscard]] std::optional<std::vector<RowSpans>> Rows()
	{
		if (_text.substr(_at).starts_with(k_ByteOrderMark))
		{
			_at += k_ByteOrderMark.size();
		}
		SkipSpace();
		if (!Take('['))
		{
			return std::nullopt;
		}
		std::vector<RowSpans> rows;
		SkipSpace();
		if (Take(']'))
		{
			return rows;
		}
		while (true)
		{
			SkipSpace();
			auto row = Row();
			if (!row)
			{
				return std::nullopt;
			}
			rows.push_back(std::move(*row));
			SkipSpace();
			if (Take(']'))
			{
				return rows;
			}
			if (!Take(','))
			{
				return std::nullopt;
			}
		}
	}

private:
	[[nodiscard]] std::optional<RowSpans> Row()
	{
		if (!Take('{'))
		{
			return std::nullopt;
		}
		RowSpans fields;
		SkipSpace();
		if (Take('}'))
		{
			return fields;
		}
		while (true)
		{
			SkipSpace();
			const auto keyBegin = _at;
			if (!SkipString())
			{
				return std::nullopt;
			}
			const auto key = OrderedJson::parse(_text.substr(keyBegin, _at - keyBegin), nullptr, false);
			if (!key.is_string())
			{
				return std::nullopt;
			}
			SkipSpace();
			if (!Take(':'))
			{
				return std::nullopt;
			}
			SkipSpace();
			const auto valueBegin = _at;
			if (!SkipValue(0))
			{
				return std::nullopt;
			}
			fields.emplace_back(key.get<std::string>(), ValueSpan {.begin = valueBegin, .end = _at});
			SkipSpace();
			if (Take('}'))
			{
				return fields;
			}
			if (!Take(','))
			{
				return std::nullopt;
			}
		}
	}

	[[nodiscard]] bool SkipValue(int depth)
	{
		constexpr int k_MaxDepth = 64;
		if (_at >= _text.size() || depth > k_MaxDepth)
		{
			return false;
		}
		const char c = _text[_at];
		if (c == '"')
		{
			return SkipString();
		}
		if (c == '{' || c == '[')
		{
			const char close = c == '{' ? '}' : ']';
			++_at;
			SkipSpace();
			if (Take(close))
			{
				return true;
			}
			while (true)
			{
				SkipSpace();
				if (c == '{')
				{
					if (!SkipString())
					{
						return false;
					}
					SkipSpace();
					if (!Take(':'))
					{
						return false;
					}
					SkipSpace();
				}
				if (!SkipValue(depth + 1))
				{
					return false;
				}
				SkipSpace();
				if (Take(close))
				{
					return true;
				}
				if (!Take(','))
				{
					return false;
				}
			}
		}
		// A number, true, false or null: the characters up to the next separator.
		const auto begin = _at;
		while (_at < _text.size() && std::string_view(",]} \t\r\n").find(_text[_at]) == std::string_view::npos)
		{
			++_at;
		}
		return _at > begin;
	}

	[[nodiscard]] bool SkipString()
	{
		if (!Take('"'))
		{
			return false;
		}
		while (_at < _text.size())
		{
			const char c = _text[_at++];
			if (c == '\\')
			{
				++_at;
			}
			else if (c == '"')
			{
				return true;
			}
		}
		return false;
	}

	void SkipSpace()
	{
		while (_at < _text.size() && std::string_view(" \t\r\n").find(_text[_at]) != std::string_view::npos)
		{
			++_at;
		}
	}

	[[nodiscard]] bool Take(char c)
	{
		if (_at < _text.size() && _text[_at] == c)
		{
			++_at;
			return true;
		}
		return false;
	}

	std::string_view _text;
	size_t _at = 0;
};

} // namespace

moa::Result<std::string> PatchKnackFields(std::string_view tableText, std::span<const KnackFieldPatch> patches,
                                          std::string_view keyField)
{
	auto rows = TableScanner(tableText).Rows();
	if (!rows)
	{
		return moa::MakeError("the table is not a JSON array of objects");
	}
	struct Change
	{
		ValueSpan span;
		std::string text;
	};
	std::vector<Change> changes;
	for (const auto& patch : patches)
	{
		if (OrderedJson::parse(patch.jsonValue, nullptr, false).is_discarded())
		{
			return moa::MakeError("{}.{}: the new value is not JSON", patch.key, patch.field);
		}
		const TableScanner::RowSpans* match = nullptr;
		for (const auto& row : *rows)
		{
			const auto key = std::ranges::find(row, keyField, &std::pair<std::string, ValueSpan>::first);
			if (key == row.end())
			{
				continue;
			}
			const auto value =
			    OrderedJson::parse(tableText.substr(key->second.begin, key->second.end - key->second.begin), nullptr, false);
			if (value.is_string() && value.get<std::string>() == patch.key)
			{
				if (match != nullptr)
				{
					return moa::MakeError("{}: two rows have this {}", patch.key, keyField);
				}
				match = &row;
			}
		}
		if (match == nullptr)
		{
			return moa::MakeError("{}: no row has this {}", patch.key, keyField);
		}
		const auto field = std::ranges::find(*match, patch.field, &std::pair<std::string, ValueSpan>::first);
		if (field == match->end())
		{
			return moa::MakeError("{}: the row has no field {}", patch.key, patch.field);
		}
		if (std::ranges::any_of(changes, [&](const Change& other) { return other.span.begin == field->second.begin; }))
		{
			return moa::MakeError("{}.{}: two changes of the same field", patch.key, patch.field);
		}
		changes.push_back({.span = field->second, .text = patch.jsonValue});
	}
	std::ranges::sort(changes, {}, [](const Change& change) { return change.span.begin; });
	std::string result;
	result.reserve(tableText.size());
	size_t at = 0;
	for (const auto& change : changes)
	{
		result.append(tableText.substr(at, change.span.begin - at));
		result.append(change.text);
		at = change.span.end;
	}
	result.append(tableText.substr(at));
	return result;
}

std::string FormatKnackValue(const KnackValue& value, std::string_view templateValue)
{
	const auto form = OrderedJson::parse(templateValue, nullptr, false);
	const auto& shape = form.is_discarded() ? OrderedJson(std::string {}) : form;
	if (const auto* text = std::get_if<std::string>(&value))
	{
		return Quote(*text);
	}
	if (const auto* truth = std::get_if<bool>(&value))
	{
		return FormatBool(*truth, shape);
	}
	return FormatNumber(std::get<double>(value), shape);
}

moa::Result<KnackMerge> MergeKnackRows(std::string_view tableText, std::span<const KnackPackRows> packs,
                                       const KnackTableRules& rules)
{
	auto body = tableText;
	if (body.starts_with(k_ByteOrderMark))
	{
		body.remove_prefix(k_ByteOrderMark.size());
	}
	const auto table = OrderedJson::parse(body, nullptr, false);
	if (table.is_discarded() || !table.is_array())
	{
		return moa::MakeError("the table is not a JSON array");
	}
	const auto end = tableText.find_last_not_of(" \t\r\n");
	if (end == std::string_view::npos || tableText[end] != ']')
	{
		return moa::MakeError("the table does not end with \"]\"");
	}

	std::vector<KnownRow> gameRows;
	gameRows.reserve(table.size());
	for (size_t i = 0; i < table.size(); ++i)
	{
		const auto& item = table[i];
		if (!item.is_object())
		{
			return moa::MakeError("the item {} of the table is not an object", i);
		}
		KnownRow row;
		for (const auto& [key, value] : item.items())
		{
			row.fields.emplace_back(key, value.dump(-1, ' ', false, OrderedJson::error_handler_t::replace));
		}
		row.name = FieldValueText(row.fields, rules.keyField);
		if (!rules.levelField.empty())
		{
			row.level = FieldValueText(row.fields, rules.levelField);
		}
		gameRows.push_back(std::move(row));
	}

	KnackMerge merge;
	std::vector<KnownRow> added;
	std::string inserted;
	bool empty = table.empty();
	for (size_t packIndex = 0; packIndex < packs.size(); ++packIndex)
	{
		const auto& pack = packs[packIndex];
		std::vector<KnownRow> packAdded = added;
		std::vector<KnackAddedRow> packRows;
		std::optional<moa::Error> error;
		for (size_t rowIndex = 0; rowIndex < pack.rows.size(); ++rowIndex)
		{
			auto row = MakeRow(pack, pack.rows[rowIndex], gameRows, packAdded, rules);
			if (!row)
			{
				error = row.GetError();
				break;
			}
			packRows.push_back({.pack = packIndex, .row = rowIndex, .name = row->name, .fields = row->fields});
			packAdded.push_back(std::move(*row));
		}
		if (error)
		{
			merge.errors.emplace_back(pack.pack, std::move(error->message));
			continue;
		}
		added = std::move(packAdded);
		for (auto& row : packRows)
		{
			if (!empty)
			{
				inserted += ',';
			}
			empty = false;
			inserted += SerialiseRow(row.fields);
			merge.added.push_back(std::move(row));
		}
	}

	merge.text.reserve(tableText.size() + inserted.size());
	merge.text.append(tableText.substr(0, end));
	merge.text.append(inserted);
	merge.text.append(tableText.substr(end));
	return merge;
}

std::string_view KnackField(const KnackAddedRow& row, std::string_view key)
{
	const auto found = std::ranges::find(row.fields, key, &std::pair<std::string, std::string>::first);
	return found == row.fields.end() ? std::string_view {} : std::string_view(found->second);
}

std::string KnackFieldText(const KnackAddedRow& row, std::string_view key)
{
	return FieldValueText(row.fields, key);
}

} // namespace moa_mod::objects
