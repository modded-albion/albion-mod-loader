/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstddef>

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <common/Result.h>

namespace moa_mod::objects
{

/// A value that a pack puts into a field of a row of a Knack table. The merge writes the value in the form of the
/// value of the template row (see `FormatKnackValue`).
using KnackValue = std::variant<std::string, double, bool>;

/// Return the text of a value in the form of the value of the template row. `templateValue` is the JSON text of the
/// template value (for example `"25.00"` with the quotes, `25` or `true`). The result is JSON text:
///
/// - A number into a text with a decimal point (`"25.00"`): a text with the same count of decimals (`"7.50"`), or with
///   more digits when those decimals change the value (`"0.125"`). Into an empty text: two decimals.
/// - A number into a JSON number: a whole number when the value has no fraction (`30`), else the shortest text.
/// - A number into another text (for example `"5"`): a text with the shortest form of the number.
/// - A truth value into a text: `"True"` or `"False"`, as the game writes them. Into a JSON truth value: `true` or
///   `false`. Into a number: `1` or `0`.
/// - A text: a JSON text, always.
[[nodiscard]] std::string FormatKnackValue(const KnackValue& value, std::string_view templateValue);

/// One row that a pack adds to a Knack table: a copy of a template row with some fields changed.
struct KnackRowRequest
{
	std::string name;         ///< The value of the key field of the new row.
	std::string templateName; ///< The value of the key field of the template row.
	/// With `KnackTableRules::levelField`: the level of the template row (its text, for example "5"). Empty: the first
	/// row with the key.
	std::string templateLevel;
	/// The fields to set, in order. A field that the template row does not have goes to the end of the row.
	std::vector<std::pair<std::string, KnackValue>> values;
	/// The fields whose values must be unique in the table, for example the id or the name of a prefab. The merge
	/// compares them with the game rows and the rows of the packs before it. An empty value is not compared.
	std::vector<std::string> uniqueFields;
};

/// The rows of one pack for one table. The merge adds all of them, or none of them.
struct KnackPackRows
{
	std::string pack; ///< The id of the pack, for the messages.
	std::vector<KnackRowRequest> rows;
};

/// The rules of one Knack table.
struct KnackTableRules
{
	std::string keyField = "m_name"; ///< The field that names a row.
	/// Compare the key with the case of ASCII letters ignored, as Unity resource paths do.
	bool keyIgnoresCase = true;
	/// Accept a new row whose key is the key of a game row. Then the pair (key, `levelField`) must be new. The hand
	/// powers use this: one name has one row for each level.
	std::string levelField;
};

/// A row that the merge added.
struct KnackAddedRow
{
	size_t pack = 0; ///< The index in the span that the merge got.
	size_t row = 0;  ///< The index in `KnackPackRows::rows`.
	std::string name;
	/// The fields of the new row, as JSON text, in the order of the row.
	std::vector<std::pair<std::string, std::string>> fields;
};

struct KnackMerge
{
	std::string text; ///< The new text of the table.
	std::vector<KnackAddedRow> added;
	/// The packs that the merge did not add: the pack id and the reason.
	std::vector<std::pair<std::string, std::string>> errors;
};

/// Add the rows of the packs to the text of a Knack table (a JSON array of objects).
///
/// The new text is the original text with the new rows before the last "]". All the other bytes stay the same, also a
/// UTF-8 byte order mark. A new row is a copy of its template row, in the field order of the template row, with the
/// key and the values of the request. The merge refuses a whole pack if one of its rows:
/// - has a key that a game row or an added row has (unless `levelField` permits it),
/// - has a unique field whose value a game row or an added row has (the case of ASCII letters is ignored),
/// - names a template that is not a game row.
///
/// Return an error only if the text is not a JSON array of objects. Then the mod must use the original text.
[[nodiscard]] moa::Result<KnackMerge> MergeKnackRows(std::string_view tableText, std::span<const KnackPackRows> packs,
                                                     const KnackTableRules& rules = {});

/// A change of one field of one game row: the row whose key field has the value `key` gets `jsonValue` (JSON text) in
/// the field `field`.
struct KnackFieldPatch
{
	std::string key;
	std::string field;
	std::string jsonValue;
};

/// Change fields of game rows in the text of a Knack table. Only the bytes of the changed values change; all other
/// bytes stay the same. Return an error, and change nothing, if the text is not a JSON array of objects, if no row or
/// more than one row has the key, or if the row does not have the field.
[[nodiscard]] moa::Result<std::string> PatchKnackFields(std::string_view tableText, std::span<const KnackFieldPatch> patches,
                                                        std::string_view keyField = "m_name");

/// Return the JSON text of a field of an added row, or an empty text when the row does not have the field.
[[nodiscard]] std::string_view KnackField(const KnackAddedRow& row, std::string_view key);

/// Return the text of a JSON text value (with no quotes), or the JSON text itself for other values.
[[nodiscard]] std::string KnackFieldText(const KnackAddedRow& row, std::string_view key);

} // namespace moa_mod::objects
