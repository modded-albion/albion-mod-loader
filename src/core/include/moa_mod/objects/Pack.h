/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <moa/common/Result.h>
#include <moa_mod/objects/KnackMerge.h>

namespace moa_mod::objects
{

// A pack is a folder in `%LOCALAPPDATA%\MastersOfAlbionReforged\packs\<folder>\` (or `packs/<folder>/` in a mod zip)
// with a `pack.json` file. The file adds custom objects to the game. Format 1 has only carried resources (rows of the
// table `NGCarriableResources`). Format 2 adds lists of typed items. The note `docs/research/mods/custom-content.md`
// gives each key of format 2.
//
//   {
//     "format": 1,                       (required: 1, or 2 for the typed items)
//     "id": "example",                   (required, [a-z0-9_-]{1,32}, unique in all packs)
//     "name": "Example pack",            (optional text, default: the id)
//     "version": "1.0.0",                (optional text, default: empty)
//     "description": "...",              (optional text, default: empty)
//     "resources": [                     (required in format 1, optional in format 2: an array, can be empty)
//       {
//         "name": "Custom_TestCotton",   (required, [A-Za-z][A-Za-z0-9_]{0,63}: the m_name of the row and the
//                                         name of the root GameObject)
//         "template": "Cotton",          (required: the m_name of a game row; the mod clones its prefab)
//         "prefab": "Custom_TestCotton", (optional, the same rule as "name", default: the name; the m_prefab)
//         "id": "0123456789abcdef01234567", (optional, 24 lower-case hex digits, default: see DefaultResourceId)
//         "model": "models/test_cotton.glb", (optional: the source model. A relative path in the pack folder,
//                                         with "/" or "\" between the parts and no "." or ".." part)
//         "mesh": "models/test_cotton.moamesh", (optional: the runtime mesh that `moa_modeltool new-object`
//                                         made from the model, the same path rule; absent: the visual of the
//                                         template stays)
//         "materials": [                 (optional: one item for each sub-mesh of the mesh, in order)
//           {"name": "Cotton", "base": "models/cotton_base.png", "normal": "models/cotton_normal.png",
//            "mask": "models/cotton_mask.png"} (each texture is optional; the same path rule)
//         ],
//         "scale": 1.0,                  (optional number, more than 0, default 1: the mod multiplies the mesh
//                                         with it; the model tool puts the size into the mesh)
//         "pivot": "bottom-centre",      (optional: "bottom-centre" (default), "centre", "origin" or [x, y, z];
//                                         the model tool puts the pivot into the mesh)
//         "collider": "template",        (optional: "template" (default), "box" or "mesh")
//         "credits": {"title": "...", "author": "...", "source": "https://...", "licence": "CC-BY-4.0"},
//                                        (optional text values: the attribution of the model)
//         "row": {                       (optional: text values for the row; each value is a JSON string)
//           "m_title": "Test Cotton", "m_description": "...", "m_sprite": "...", "m_textSprite": "",
//           "m_spriteColour": "#B76711FF", (empty or "#RRGGBBAA")
//           "m_bezierColor": ""          (empty, "#RRGGBB" or "#RRGGBBAA")
//         }
//       }
//     ],
//     "throwables": [                    (format 2, optional: decorations that the hand can pick up and throw)
//       {
//         "name", "template", "prefab", "id", "model", "mesh", "materials", "scale", "pivot", "collider",
//         "credits"                      (as for a resource; the template is the m_name of a decoration row)
//         "grip": [x, y, z],             (optional: the point where the hand holds the object, in metres in the
//                                         space of the prefab; absent: the centre of the top of the mesh)
//         "up": "z",                     (optional: "y" (default) or "z", the up axis of the source model)
//         "title": "Rock", "mass": 200, ... (optional typed values, see `PackFields`)
//       }
//     ],
//     "weapons": [                       (format 2, optional: parts of weapon designs, for example sword blades)
//       {
//         the keys of a throwable, with "tip" in place of "grip": the point of the blade, where the next part of
//         the design snaps; the template is the m_prefabName of a block row, for example "Weapon_Blade_Sword_A"
//       }
//     ],
//     "spells": [                        (format 2, optional: new levels of the hand powers)
//       {
//         "power": "Fireball",           (required: one of `k_SpellPowers`)
//         "level": 6,                    (required: the new level; see `PackFields`)
//         "id": "...",                   (optional, 24 lower-case hex digits: the id of the hand power row)
//         "manaCost": 40, ...            (optional typed values)
//       }
//     ]
//   }
//
// The parser refuses a key that it does not know, at each level. The row cannot set "id", "m_name" or "m_prefab":
// the keys of the resource set them. A field that the row does not give gets the value of the template row.

/// The first format: carried resources only. A pack that has only carried resources keeps this format.
constexpr int k_PackFormat = 1;
/// The newest format: the typed lists of items.
constexpr int k_PackFormatTyped = 2;

/// The kind of a custom object of a pack.
enum class PackKind : uint8_t
{
	Resource,  ///< A carried resource: a row of `NGCarriableResources` and a prefab in `_Prefabs/RawMaterials/`.
	Throwable, ///< A decoration that the hand can pick up and throw: a row of `NGDecorationInfo`.
	Weapon,    ///< A part of a weapon design, for example a sword blade: a row of `NGBlockInfo`.
	Spell,     ///< A new level of a hand power: a row of `MAHandPowerInfo` and a row of `MAResearchInfo`.
};

/// The hand powers that a spell can have a new level of. The game has a level for each of them.
inline constexpr std::array<std::string_view, 6> k_SpellPowers {"Dig",       "Fireball",  "Flamethrower",
                                                                "Lightning", "WaterBlob", "WaterSpout"};

/// Return true if the kind has a model: a prefab that the mod makes from a template.
[[nodiscard]] constexpr bool HasModel(PackKind kind)
{
	return kind != PackKind::Spell;
}

/// The JSON list of a pack that holds the items of a kind.
[[nodiscard]] std::string_view PackListName(PackKind kind);

/// The type of a typed value of a pack item.
enum class PackFieldType : uint8_t
{
	Text,    ///< A JSON text.
	Number,  ///< A JSON number in the limits.
	Integer, ///< A JSON number with no fraction, in the limits.
	Truth,   ///< A JSON truth value.
};

/// One typed value of a pack item: the key in `pack.json`, the field of the game table that gets the value, and the
/// limits of the value.
struct PackField
{
	std::string_view key;
	std::string_view field;
	PackFieldType type = PackFieldType::Text;
	double min = 0.0;
	double max = 0.0;
	std::string_view help; ///< What the value does in the game, for the editor and the documents.
};

/// The typed values that the items of a kind can have, in the order that the writer uses. A carried resource has no
/// typed values (it uses "row").
[[nodiscard]] std::span<const PackField> PackFields(PackKind kind);

/// The key of the point of an item in `pack.json`: "grip" for a throwable, "tip" for a weapon, else empty.
[[nodiscard]] std::string_view PackPointKey(PackKind kind);

/// Return the name of the item of a spell: the power and the level, for example "Fireball6".
[[nodiscard]] std::string SpellItemName(std::string_view power, int level);

/// Return the field of a kind with this key, or null.
[[nodiscard]] const PackField* FindPackField(PackKind kind, std::string_view key);

/// The point of the model that goes to the origin of the new prefab.
enum class PivotMode : uint8_t
{
	BottomCentre, ///< The centre of the bounds in X and Z, and the lowest Y.
	Centre,       ///< The centre of the bounds.
	Origin,       ///< The origin of the model does not move.
	Explicit,     ///< The point in `PackResource::pivotPoint`.
};

/// The collider of the new prefab.
enum class ColliderMode : uint8_t
{
	Template, ///< Keep the collider of the template.
	Box,      ///< A box that fits the bounds of the model.
	Mesh,     ///< A mesh collider with the mesh of the model.
};

/// The game fields of a row of `NGCarriableResources`, in the order of the game.
inline constexpr std::array<std::string_view, 9> k_CarriableFields {
    "id", "m_name", "m_title", "m_prefab", "m_description", "m_textSprite", "m_sprite", "m_spriteColour", "m_bezierColor",
};

/// The textures of one material of a pack mesh. Each path is relative to the pack folder, with "/" between the parts.
struct PackMaterial
{
	std::string name;
	std::optional<std::string> base;   ///< PNG, sRGB: the base colour (`_BaseMap`).
	std::optional<std::string> normal; ///< PNG, linear: the tangent-space normal map (`_BumpMap`).
	std::optional<std::string> mask;   ///< PNG, linear: R metallic, G occlusion, A smoothness (`_MaskMap`).

	[[nodiscard]] bool operator==(const PackMaterial&) const = default;
};

/// The attribution of a model. Each field can be empty.
struct PackCredits
{
	std::string title;
	std::string author;
	std::string source;
	std::string licence;

	[[nodiscard]] bool Empty() const { return title.empty() && author.empty() && source.empty() && licence.empty(); }
	[[nodiscard]] bool operator==(const PackCredits&) const = default;
};

/// One custom carried resource of a pack.
struct PackResource
{
	std::string name;
	std::string templateName;
	std::string prefab;               ///< The m_prefab of the row. The parser sets the name when the file gives none.
	std::optional<std::string> id;    ///< Absent: the merge makes the id with `DefaultResourceId`.
	std::optional<std::string> model; ///< The source model. Relative to the pack folder, with "/" between the parts.
	std::optional<std::string> mesh;  ///< The runtime mesh (MOARMESH). The same path rule.
	std::vector<PackMaterial> materials;
	float scale = 1.0f;
	PivotMode pivot = PivotMode::BottomCentre;
	std::array<float, 3> pivotPoint {0.0f, 0.0f, 0.0f};
	ColliderMode collider = ColliderMode::Template;
	/// The row fields that the file gives, in the order of the file.
	std::vector<std::pair<std::string, std::string>> row;
	PackCredits credits;
	PackKind kind = PackKind::Resource;
	/// The typed values of a typed item, in the order of `PackFields`. Each value has the type of its field.
	std::vector<std::pair<std::string, KnackValue>> values;
	/// For a throwable, the point where the hand holds the object ("grip"); for a weapon, the point of the blade
	/// ("tip"). In metres from the pivot. Absent: the centre of the top of the mesh.
	std::optional<std::array<float, 3>> grip;
	/// The source model has Z up ("up": "z"). Only the editor uses it, to bake the source model again; the mesh is
	/// always Y up.
	bool upZ = false;
};

/// The contents of one `pack.json` file.
struct Pack
{
	std::string folder; ///< The name of the pack folder. `ParsePacks` sets it; `ParsePack` leaves it empty.
	std::string id;
	std::string name;
	std::string version;
	std::string description;
	/// The items of all kinds, in the order of the lists in the file: the carried resources, the throwables, the
	/// weapons, then the spells.
	std::vector<PackResource> resources;
	int format = k_PackFormat; ///< The format of the file.
};

/// Return the value of a typed field of an item, or no value.
[[nodiscard]] std::optional<KnackValue> PackValue(const PackResource& item, std::string_view key);

/// Read and check the text of a `pack.json` file. An error message starts with the path of the bad field, for
/// example `resources[1].row.m_title: ...`.
[[nodiscard]] moa::Result<Pack> ParsePack(std::string_view text);

/// Make the id of a resource that has no id in the file: 24 lower-case hex digits from the text
/// `<pack id> + "/" + <resource name>`. The first 16 digits are the FNV-1a 64-bit hash of the text, the last 8
/// digits are the FNV-1a 32-bit hash of the text. The bytes of the text go into the hash, so the id is the same on
/// each run and each computer.
[[nodiscard]] std::string DefaultResourceId(std::string_view packId, std::string_view resourceName);

/// The name of a pack folder and the text of its `pack.json` file.
struct PackSource
{
	std::string folder;
	std::string text;
};

/// A problem with one pack (or one pack folder) that stopped its use.
struct PackError
{
	std::string pack; ///< The folder name, or the pack id when the folder is not known.
	std::string message;
};

/// The packs that are correct, and the errors of the others.
struct PackSet
{
	std::vector<Pack> packs; ///< Sorted by id.
	std::vector<PackError> errors;
};

/// Read all the pack files. A bad pack does not stop the others: the result does not include it and gets its error.
/// A pack with the same id as a pack before it (sorted by folder name) is bad.
[[nodiscard]] PackSet ParsePacks(std::span<const PackSource> sources);

/// Change "\" to "/" in a path of a pack file. Return nothing if the path is absolute, is empty, has an empty part, or
/// has a "." or ".." part, because the path must stay in the pack folder.
[[nodiscard]] std::optional<std::string> NormalisePackPath(std::string_view text);

/// Return true if two texts are equal when the case of ASCII letters is ignored. Unity resource paths ignore case.
[[nodiscard]] bool EqualsIgnoreCase(std::string_view a, std::string_view b);

} // namespace moa_mod::objects
