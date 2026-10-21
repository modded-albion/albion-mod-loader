/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "core/Pack.h"

#include <cmath>
#include <cstdint>

#include <algorithm>
#include <array>
#include <format>
#include <map>

#include <nlohmann/json.hpp>

namespace moa_mod::objects
{
namespace
{

constexpr size_t k_MaxPackIdLength = 32;
constexpr size_t k_MaxResourceNameLength = 64;
constexpr size_t k_ResourceIdLength = 24;
constexpr size_t k_MaxTextLength = 2000;

/// The typed values of a weapon part: the fields of `NGBlockInfo` that a pack can set.
constexpr std::array k_WeaponFields {
    PackField {.key = "title", .field = "m_displayName", .type = PackFieldType::Text, .help = "The name that the game shows."},
    PackField {.key = "description",
               .field = "m_description",
               .type = PackFieldType::Text,
               .help = "The text of the information panel."},
    PackField {.key = "attack",
               .field = "m_attack",
               .type = PackFieldType::Number,
               .min = 0.0,
               .max = 10.0,
               .help = "The attack of the part. The sum of the parts is the damage of the weapon. Blades: 0.40 to 0.90."},
    PackField {.key = "defence",
               .field = "m_defence",
               .type = PackFieldType::Number,
               .min = 0.0,
               .max = 10.0,
               .help = "The defence of the part. Blades: 0.10 to 0.40."},
    PackField {.key = "beauty",
               .field = "m_beauty",
               .type = PackFieldType::Number,
               .min = 0.0,
               .max = 10.0,
               .help = "The beauty score of the part."},
    PackField {.key = "weight",
               .field = "m_weight",
               .type = PackFieldType::Number,
               .min = 0.1,
               .max = 100.0,
               .help = "The weight of the part. A weapon of 25 or more is medium, 50 or more is heavy."},
    PackField {.key = "price",
               .field = "m_price",
               .type = PackFieldType::Number,
               .min = 0.0,
               .max = 1000000.0,
               .help = "The cost to make the part."},
    PackField {.key = "sellingPrice",
               .field = "m_sellingPrice",
               .type = PackFieldType::Number,
               .min = 0.0,
               .max = 1000000.0,
               .help = "The selling price of the part."},
    PackField {.key = "rarity",
               .field = "m_rarity",
               .type = PackFieldType::Text,
               .help = "Common, Uncommon, Rare, Epic or Legendary."},
    PackField {.key = "tags",
               .field = "m_tags",
               .type = PackFieldType::Text,
               .help = "The tags, with '|' between them. A blade needs 'Blade' for the sword skill of the hero."},
    PackField {.key = "starterPack",
               .field = "m_starterPack",
               .type = PackFieldType::Truth,
               .help = "The player has the part from the start, when the weapon drawer is open."},
};

/// The typed values of a spell: the fields of `MAHandPowerInfo` and of the research row that unlocks the level.
constexpr std::array k_SpellFields {
    PackField {.key = "level",
               .field = "m_level",
               .type = PackFieldType::Integer,
               .min = 2.0,
               .max = 99.0,
               .help = "The new level. It must be the next level of the power."},
    PackField {.key = "templateLevel",
               .field = "",
               .type = PackFieldType::Integer,
               .min = 1.0,
               .max = 99.0,
               .help = "The level whose values the new level copies. Default: the highest level of the game."},
    PackField {.key = "title", .field = "m_title", .type = PackFieldType::Text, .help = "The name of the level."},
    PackField {.key = "description", .field = "m_description", .type = PackFieldType::Text, .help = "The text of the level."},
    PackField {.key = "baseDamage",
               .field = "m_baseDamage",
               .type = PackFieldType::Number,
               .min = 0.0,
               .max = 100000.0,
               .help = "The damage of a hit."},
    PackField {.key = "minDamage",
               .field = "m_minDamage",
               .type = PackFieldType::Number,
               .min = 0.0,
               .max = 100000.0,
               .help = "The smallest damage of a hit."},
    PackField {.key = "numOfHits",
               .field = "m_numOfHits",
               .type = PackFieldType::Number,
               .min = 0.0,
               .max = 1000.0,
               .help = "The number of hits."},
    PackField {.key = "knockbackPower",
               .field = "m_knockbackPower",
               .type = PackFieldType::Number,
               .min = 0.0,
               .max = 100000.0,
               .help = "The force that pushes the targets."},
    PackField {.key = "perChainDamageModifier",
               .field = "m_perChainDamageModifier",
               .type = PackFieldType::Number,
               .min = 0.0,
               .max = 100.0,
               .help = "The change of the damage at each jump of a chain."},
    PackField {.key = "aoeRadius",
               .field = "m_aoeRadius",
               .type = PackFieldType::Number,
               .min = 0.0,
               .max = 1000.0,
               .help = "The radius of the area, in metres."},
    PackField {.key = "attackWidth",
               .field = "m_attackWidth",
               .type = PackFieldType::Number,
               .min = 0.0,
               .max = 1000.0,
               .help = "The width of the attack, in metres."},
    PackField {.key = "attackLength",
               .field = "m_attackLength",
               .type = PackFieldType::Number,
               .min = 0.0,
               .max = 1000.0,
               .help = "The length of the attack, in metres."},
    PackField {.key = "bonusAttackChance",
               .field = "m_bonusAttackChance",
               .type = PackFieldType::Number,
               .min = 0.0,
               .max = 1.0,
               .help = "The chance of a bonus attack, from 0 to 1."},
    PackField {.key = "tickSpeed",
               .field = "m_tickSpeed",
               .type = PackFieldType::Number,
               .min = 0.0,
               .max = 1000.0,
               .help = "The time between two hits of a power that the player holds."},
    PackField {.key = "manaCost",
               .field = "m_manaCost",
               .type = PackFieldType::Number,
               .min = 0.0,
               .max = 100000.0,
               .help = "The mana for one cast."},
    PackField {.key = "manaCostPerSecond",
               .field = "m_manaCostPerSecond",
               .type = PackFieldType::Number,
               .min = 0.0,
               .max = 100000.0,
               .help = "The mana for each second that the player holds the power."},
    PackField {.key = "baseCooldownTime",
               .field = "m_baseCooldownTime",
               .type = PackFieldType::Number,
               .min = 0.0,
               .max = 10000.0,
               .help = "The time before the next cast, in seconds."},
    PackField {.key = "researchTitle",
               .field = "m_title",
               .type = PackFieldType::Text,
               .help = "The name of the research that unlocks the level."},
    PackField {.key = "researchDescription",
               .field = "m_description",
               .type = PackFieldType::Text,
               .help = "The text of the research that unlocks the level."},
    PackField {.key = "dollarCost",
               .field = "m_dollarCost",
               .type = PackFieldType::Integer,
               .min = 0.0,
               .max = 100000000.0,
               .help = "The money cost of the research."},
    PackField {.key = "factionCost",
               .field = "m_factionCost",
               .type = PackFieldType::Integer,
               .min = 0.0,
               .max = 1000000.0,
               .help = "The faction cost of the research."},
    PackField {.key = "icon",
               .field = "m_icon",
               .type = PackFieldType::Text,
               .help = "The icon of the research: a name in _art/Icons, for example ARC_fireball."},
};

/// The typed values of a throwable: the fields of `NGDecorationInfo` that a pack can set.
constexpr std::array k_ThrowableFields {
    PackField {.key = "title", .field = "m_displayName", .type = PackFieldType::Text, .help = "The name that the game shows."},
    PackField {.key = "description",
               .field = "m_description",
               .type = PackFieldType::Text,
               .help = "The text of the information panel."},
    PackField {.key = "mass",
               .field = "m_throwableMass",
               .type = PackFieldType::Number,
               .min = 1.0,
               .max = 100000.0,
               .help = "The mass of the thrown object. The game objects use 200 (a barrel) to 5000 (a large boulder)."},
    PackField {.key = "damage",
               .field = "m_throwExplosionDamageBase",
               .type = PackFieldType::Number,
               .min = 0.0,
               .max = 100000.0,
               .help = "The damage of the impact. The game objects use 10 to 250."},
    PackField {.key = "power",
               .field = "m_throwExplosionPowerBase",
               .type = PackFieldType::Number,
               .min = 0.0,
               .max = 100000.0,
               .help = "The force that the impact gives to the objects near it."},
    PackField {.key = "radius",
               .field = "m_throwExplosionRadiusBase",
               .type = PackFieldType::Number,
               .min = 0.0,
               .max = 1000.0,
               .help = "The radius of the impact, in metres."},
    PackField {.key = "speed",
               .field = "m_throwExplosionSpeedBase",
               .type = PackFieldType::Number,
               .min = 0.1,
               .max = 1000.0,
               .help = "The speed that the object must have to make an impact. The game divides by it."},
    PackField {.key = "maxImpacts",
               .field = "m_maxImpactCount",
               .type = PackFieldType::Integer,
               .min = 0.0,
               .max = 10000.0,
               .help = "The impacts before the object breaks. 0: no limit."},
    PackField {.key = "health",
               .field = "m_health",
               .type = PackFieldType::Number,
               .min = 0.0,
               .max = 100000.0,
               .help = "The health of the object."},
    PackField {.key = "price",
               .field = "m_price",
               .type = PackFieldType::Integer,
               .min = 0.0,
               .max = 1000000.0,
               .help = "The price on the decoration strip."},
    PackField {.key = "starterPack",
               .field = "m_starterPack",
               .type = PackFieldType::Truth,
               .help = "The player has the object from the start."},
    PackField {.key = "onDecorationStrip",
               .field = "m_isOnDecorationStrip",
               .type = PackFieldType::Truth,
               .help = "The decoration strip shows the object."},
};

[[nodiscard]] constexpr bool IsLower(char c)
{
	return c >= 'a' && c <= 'z';
}

[[nodiscard]] constexpr bool IsUpper(char c)
{
	return c >= 'A' && c <= 'Z';
}

[[nodiscard]] constexpr bool IsDigit(char c)
{
	return c >= '0' && c <= '9';
}

[[nodiscard]] constexpr bool IsHex(char c)
{
	return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

[[nodiscard]] constexpr char ToLower(char c)
{
	return IsUpper(c) ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] bool IsPackId(std::string_view text)
{
	return !text.empty() && text.size() <= k_MaxPackIdLength &&
	       std::ranges::all_of(text, [](char c) { return IsLower(c) || IsDigit(c) || c == '_' || c == '-'; });
}

[[nodiscard]] bool IsResourceName(std::string_view text)
{
	return !text.empty() && text.size() <= k_MaxResourceNameLength && (IsLower(text[0]) || IsUpper(text[0])) &&
	       std::ranges::all_of(text, [](char c) { return IsLower(c) || IsUpper(c) || IsDigit(c) || c == '_'; });
}

[[nodiscard]] bool IsResourceId(std::string_view text)
{
	return text.size() == k_ResourceIdLength &&
	       std::ranges::all_of(text, [](char c) { return IsDigit(c) || (c >= 'a' && c <= 'f'); });
}

/// Return true for "#" and the given numbers of hex digits.
[[nodiscard]] bool IsColour(std::string_view text, std::initializer_list<size_t> digitCounts)
{
	if (text.empty() || text[0] != '#')
	{
		return false;
	}
	const auto digits = text.substr(1);
	return std::ranges::find(digitCounts, digits.size()) != digitCounts.end() && std::ranges::all_of(digits, IsHex);
}

using Json = nlohmann::json;

/// Return an error for each key of an object that is not in the list.
[[nodiscard]] moa::Result<void> CheckKeys(const Json& object, std::string_view path,
                                          std::initializer_list<std::string_view> known)
{
	for (const auto& [key, value] : object.items())
	{
		if (std::ranges::find(known, key) == known.end())
		{
			return moa::MakeError("{}{}{}: the key is not known", path, path.empty() ? "" : ".", key);
		}
	}
	return {};
}

[[nodiscard]] std::string Join(std::string_view path, std::string_view key)
{
	return path.empty() ? std::string(key) : std::format("{}.{}", path, key);
}

/// Read an optional text field. Return an error if the field is there and is not text.
[[nodiscard]] moa::Result<std::optional<std::string>> ReadText(const Json& object, std::string_view path, const char* key)
{
	const auto found = object.find(key);
	if (found == object.end())
	{
		return std::optional<std::string> {};
	}
	if (!found->is_string())
	{
		return moa::MakeError("{}: the value must be text", Join(path, key));
	}
	return std::optional<std::string> {found->get<std::string>()};
}

[[nodiscard]] moa::Result<float> ReadScale(const Json& object, std::string_view path)
{
	const auto found = object.find("scale");
	if (found == object.end())
	{
		return 1.0f;
	}
	if (!found->is_number())
	{
		return moa::MakeError("{}: the value must be a number", Join(path, "scale"));
	}
	const auto value = found->get<double>();
	if (!std::isfinite(value) || value <= 0.0 || value > 1.0e6)
	{
		return moa::MakeError("{}: the value must be more than 0 and not more than 1000000", Join(path, "scale"));
	}
	return static_cast<float>(value);
}

[[nodiscard]] moa::Result<void> ReadPivot(const Json& object, std::string_view path, PackResource& resource)
{
	const auto found = object.find("pivot");
	if (found == object.end())
	{
		return {};
	}
	const auto field = Join(path, "pivot");
	if (found->is_string())
	{
		const auto text = found->get<std::string>();
		if (text == "bottom-centre")
		{
			resource.pivot = PivotMode::BottomCentre;
		}
		else if (text == "centre")
		{
			resource.pivot = PivotMode::Centre;
		}
		else if (text == "origin")
		{
			resource.pivot = PivotMode::Origin;
		}
		else
		{
			return moa::MakeError("{}: use \"bottom-centre\", \"centre\", \"origin\" or [x, y, z]", field);
		}
		return {};
	}
	if (!found->is_array() || found->size() != 3)
	{
		return moa::MakeError("{}: use \"bottom-centre\", \"centre\", \"origin\" or [x, y, z]", field);
	}
	for (size_t i = 0; i < 3; ++i)
	{
		const auto& item = (*found)[i];
		if (!item.is_number() || !std::isfinite(item.get<double>()))
		{
			return moa::MakeError("{}[{}]: the value must be a number", field, i);
		}
		resource.pivotPoint[i] = item.get<float>();
	}
	resource.pivot = PivotMode::Explicit;
	return {};
}

[[nodiscard]] moa::Result<void> ReadCollider(const Json& object, std::string_view path, PackResource& resource)
{
	auto text = ReadText(object, path, "collider");
	if (!text)
	{
		return text.GetError();
	}
	if (!text->has_value())
	{
		return {};
	}
	if (**text == "template")
	{
		resource.collider = ColliderMode::Template;
	}
	else if (**text == "box")
	{
		resource.collider = ColliderMode::Box;
	}
	else if (**text == "mesh")
	{
		resource.collider = ColliderMode::Mesh;
	}
	else
	{
		return moa::MakeError("{}: use \"template\", \"box\" or \"mesh\"", Join(path, "collider"));
	}
	return {};
}

[[nodiscard]] moa::Result<void> ReadRow(const Json& object, std::string_view path, PackResource& resource)
{
	const auto found = object.find("row");
	if (found == object.end())
	{
		return {};
	}
	const auto rowPath = Join(path, "row");
	if (!found->is_object())
	{
		return moa::MakeError("{}: the value must be an object", rowPath);
	}
	for (const auto& [key, value] : found->items())
	{
		const auto field = Join(rowPath, key);
		if (key == "id" || key == "m_name" || key == "m_prefab")
		{
			return moa::MakeError("{}: the row cannot set this field; use the key \"{}\" of the resource", field,
			                      key == "id" ? "id" : (key == "m_name" ? "name" : "prefab"));
		}
		if (std::ranges::find(k_CarriableFields, key) == k_CarriableFields.end())
		{
			return moa::MakeError("{}: the field is not a field of NGCarriableResources", field);
		}
		if (!value.is_string())
		{
			return moa::MakeError("{}: the value must be text", field);
		}
		auto text = value.get<std::string>();
		if (key == "m_spriteColour" && !text.empty() && !IsColour(text, {8}))
		{
			return moa::MakeError("{}: the value must be empty or \"#RRGGBBAA\"", field);
		}
		if (key == "m_bezierColor" && !text.empty() && !IsColour(text, {6, 8}))
		{
			return moa::MakeError("{}: the value must be empty, \"#RRGGBB\" or \"#RRGGBBAA\"", field);
		}
		resource.row.emplace_back(key, std::move(text));
	}
	return {};
}

/// Read an optional path of a pack file.
[[nodiscard]] moa::Result<std::optional<std::string>> ReadPath(const Json& object, std::string_view path, const char* key)
{
	auto text = ReadText(object, path, key);
	if (!text)
	{
		return text.GetError();
	}
	if (!text->has_value())
	{
		return std::optional<std::string> {};
	}
	auto normal = NormalisePackPath(**text);
	if (!normal)
	{
		return moa::MakeError("{}: the path must be relative and stay in the pack folder", Join(path, key));
	}
	return normal;
}

[[nodiscard]] moa::Result<void> ReadMaterials(const Json& object, std::string_view path, PackResource& resource)
{
	const auto found = object.find("materials");
	if (found == object.end())
	{
		return {};
	}
	const auto listPath = Join(path, "materials");
	if (!found->is_array())
	{
		return moa::MakeError("{}: the value must be an array", listPath);
	}
	for (size_t i = 0; i < found->size(); ++i)
	{
		const auto& item = (*found)[i];
		const auto itemPath = std::format("{}[{}]", listPath, i);
		if (!item.is_object())
		{
			return moa::MakeError("{}: the value must be an object", itemPath);
		}
		if (auto keys = CheckKeys(item, itemPath, {"name", "base", "normal", "mask"}); !keys)
		{
			return keys.GetError();
		}
		PackMaterial material;
		auto name = ReadText(item, itemPath, "name");
		if (!name)
		{
			return name.GetError();
		}
		material.name = name->value_or("");
		for (const auto& [key, field] : {std::pair {"base", &PackMaterial::base}, std::pair {"normal", &PackMaterial::normal},
		                                 std::pair {"mask", &PackMaterial::mask}})
		{
			auto file = ReadPath(item, itemPath, key);
			if (!file)
			{
				return file.GetError();
			}
			material.*field = std::move(*file);
		}
		resource.materials.push_back(std::move(material));
	}
	return {};
}

[[nodiscard]] moa::Result<void> ReadCredits(const Json& object, std::string_view path, PackResource& resource)
{
	const auto found = object.find("credits");
	if (found == object.end())
	{
		return {};
	}
	const auto creditsPath = Join(path, "credits");
	if (!found->is_object())
	{
		return moa::MakeError("{}: the value must be an object", creditsPath);
	}
	if (auto keys = CheckKeys(*found, creditsPath, {"title", "author", "source", "licence"}); !keys)
	{
		return keys.GetError();
	}
	for (const auto& [key, field] : {std::pair {"title", &PackCredits::title}, std::pair {"author", &PackCredits::author},
	                                 std::pair {"source", &PackCredits::source}, std::pair {"licence", &PackCredits::licence}})
	{
		auto text = ReadText(*found, creditsPath, key);
		if (!text)
		{
			return text.GetError();
		}
		resource.credits.*field = text->value_or("");
	}
	return {};
}

[[nodiscard]] moa::Result<void> ReadGrip(const Json& object, std::string_view path, PackResource& resource)
{
	const auto key = PackPointKey(resource.kind);
	if (key.empty())
	{
		return {};
	}
	const auto found = object.find(std::string(key));
	if (found == object.end())
	{
		return {};
	}
	const auto field = Join(path, key);
	if (!found->is_array() || found->size() != 3)
	{
		return moa::MakeError("{}: use [x, y, z]", field);
	}
	std::array<float, 3> grip {};
	for (size_t i = 0; i < 3; ++i)
	{
		const auto& item = (*found)[i];
		if (!item.is_number() || !std::isfinite(item.get<double>()) || std::abs(item.get<double>()) > 1000.0)
		{
			return moa::MakeError("{}[{}]: the value must be a number from -1000 to 1000", field, i);
		}
		grip[i] = item.get<float>();
	}
	resource.grip = grip;
	return {};
}

[[nodiscard]] moa::Result<void> ReadUp(const Json& object, std::string_view path, PackResource& resource)
{
	auto text = ReadText(object, path, "up");
	if (!text)
	{
		return text.GetError();
	}
	if (!text->has_value() || **text == "y")
	{
		return {};
	}
	if (**text != "z")
	{
		return moa::MakeError("{}: use \"y\" or \"z\"", Join(path, "up"));
	}
	resource.upZ = true;
	return {};
}

/// Read the typed values of an item, in the order of the fields of its kind.
[[nodiscard]] moa::Result<void> ReadTypedValues(const Json& object, std::string_view path, PackResource& resource)
{
	for (const auto& spec : PackFields(resource.kind))
	{
		const auto found = object.find(std::string(spec.key));
		if (found == object.end())
		{
			continue;
		}
		const auto field = Join(path, spec.key);
		switch (spec.type)
		{
		case PackFieldType::Text:
			if (!found->is_string() || found->get<std::string>().size() > k_MaxTextLength)
			{
				return moa::MakeError("{}: the value must be text of up to {} bytes", field, k_MaxTextLength);
			}
			resource.values.emplace_back(spec.key, found->get<std::string>());
			break;
		case PackFieldType::Truth:
			if (!found->is_boolean())
			{
				return moa::MakeError("{}: the value must be true or false", field);
			}
			resource.values.emplace_back(spec.key, found->get<bool>());
			break;
		case PackFieldType::Number:
		case PackFieldType::Integer:
		{
			const bool whole = spec.type == PackFieldType::Integer;
			const auto value = found->is_number() ? found->get<double>() : std::nan("");
			if (!std::isfinite(value) || value < spec.min || value > spec.max || (whole && std::trunc(value) != value))
			{
				return moa::MakeError("{}: the value must be a {} from {} to {}", field, whole ? "whole number" : "number",
				                      spec.min, spec.max);
			}
			resource.values.emplace_back(spec.key, value);
			break;
		}
		}
	}
	return {};
}

/// Read a spell: the power, the level, the id and the typed values. The name of the item is the power and the level.
[[nodiscard]] moa::Result<PackResource> ReadSpell(const Json& object, std::string_view path)
{
	PackResource spell;
	spell.kind = PackKind::Spell;
	auto power = ReadText(object, path, "power");
	if (!power)
	{
		return power.GetError();
	}
	if (!power->has_value() || std::ranges::find(k_SpellPowers, **power) == k_SpellPowers.end())
	{
		return moa::MakeError("{}: give one of Dig, Fireball, Flamethrower, Lightning, WaterBlob, WaterSpout",
		                      Join(path, "power"));
	}
	spell.templateName = std::move(**power);
	auto id = ReadText(object, path, "id");
	if (!id)
	{
		return id.GetError();
	}
	if (id->has_value() && !IsResourceId(**id))
	{
		return moa::MakeError("{}: the id must be 24 lower-case hex digits", Join(path, "id"));
	}
	spell.id = std::move(*id);
	if (auto values = ReadTypedValues(object, path, spell); !values)
	{
		return values.GetError();
	}
	const auto level = PackValue(spell, "level");
	if (!level)
	{
		return moa::MakeError("{}: give the new level", Join(path, "level"));
	}
	spell.name = SpellItemName(spell.templateName, static_cast<int>(std::get<double>(*level)));
	spell.prefab = spell.name;
	return spell;
}

[[nodiscard]] moa::Result<PackResource> ReadResource(const Json& object, std::string_view path, PackKind kind)
{
	if (!object.is_object())
	{
		return moa::MakeError("{}: the value must be an object", path);
	}
	if (kind == PackKind::Resource)
	{
		if (auto keys = CheckKeys(object, path,
		                          {"name", "template", "prefab", "id", "model", "mesh", "materials", "scale", "pivot",
		                           "collider", "row", "credits"});
		    !keys)
		{
			return keys.GetError();
		}
	}
	else if (kind == PackKind::Spell)
	{
		for (const auto& [key, value] : object.items())
		{
			if (key != "power" && key != "id" && FindPackField(kind, key) == nullptr)
			{
				return moa::MakeError("{}: the key is not known", Join(path, key));
			}
		}
		return ReadSpell(object, path);
	}
	else
	{
		constexpr std::array<std::string_view, 12> k_Common {"up",   "name",      "template", "prefab", "id",       "model",
		                                                     "mesh", "materials", "scale",    "pivot",  "collider", "credits"};
		for (const auto& [key, value] : object.items())
		{
			if (std::ranges::find(k_Common, key) == k_Common.end() && key != PackPointKey(kind) &&
			    FindPackField(kind, key) == nullptr)
			{
				return moa::MakeError("{}: the key is not known", Join(path, key));
			}
		}
	}

	PackResource resource;
	resource.kind = kind;
	auto name = ReadText(object, path, "name");
	if (!name)
	{
		return name.GetError();
	}
	if (!name->has_value() || !IsResourceName(**name))
	{
		return moa::MakeError("{}: the name must match [A-Za-z][A-Za-z0-9_]{{0,63}}", Join(path, "name"));
	}
	resource.name = std::move(**name);

	auto templateName = ReadText(object, path, "template");
	if (!templateName)
	{
		return templateName.GetError();
	}
	if (!templateName->has_value() || (*templateName)->empty())
	{
		return moa::MakeError("{}: give the m_name of a game row", Join(path, "template"));
	}
	resource.templateName = std::move(**templateName);

	auto prefab = ReadText(object, path, "prefab");
	if (!prefab)
	{
		return prefab.GetError();
	}
	if (prefab->has_value() && !IsResourceName(**prefab))
	{
		return moa::MakeError("{}: the prefab must match [A-Za-z][A-Za-z0-9_]{{0,63}}", Join(path, "prefab"));
	}
	resource.prefab = prefab->value_or(resource.name);

	auto id = ReadText(object, path, "id");
	if (!id)
	{
		return id.GetError();
	}
	if (id->has_value() && !IsResourceId(**id))
	{
		return moa::MakeError("{}: the id must be 24 lower-case hex digits", Join(path, "id"));
	}
	resource.id = std::move(*id);

	auto model = ReadText(object, path, "model");
	if (!model)
	{
		return model.GetError();
	}
	if (model->has_value())
	{
		resource.model = NormalisePackPath(**model);
		if (!resource.model)
		{
			return moa::MakeError("{}: the path must be relative and stay in the pack folder", Join(path, "model"));
		}
	}

	auto mesh = ReadPath(object, path, "mesh");
	if (!mesh)
	{
		return mesh.GetError();
	}
	resource.mesh = std::move(*mesh);

	auto scale = ReadScale(object, path);
	if (!scale)
	{
		return scale.GetError();
	}
	resource.scale = *scale;

	for (auto* read : {&ReadPivot, &ReadCollider, &ReadRow, &ReadMaterials, &ReadCredits, &ReadGrip, &ReadUp, &ReadTypedValues})
	{
		if (auto result = read(object, path, resource); !result)
		{
			return result.GetError();
		}
	}
	return resource;
}

/// The FNV-1a hash of the bytes of a text, with 64 or 32 bits.
template <typename T, T Offset, T Prime>
[[nodiscard]] T Fnv1a(std::string_view text)
{
	T hash = Offset;
	for (const char c : text)
	{
		hash ^= static_cast<uint8_t>(c);
		hash *= Prime;
	}
	return hash;
}

/// The lists of a pack file, in the order that the parser reads them.
constexpr std::array k_Kinds {PackKind::Resource, PackKind::Throwable, PackKind::Weapon, PackKind::Spell};

} // namespace

std::string_view PackListName(PackKind kind)
{
	switch (kind)
	{
	case PackKind::Throwable:
		return "throwables";
	case PackKind::Weapon:
		return "weapons";
	case PackKind::Spell:
		return "spells";
	case PackKind::Resource:
		break;
	}
	return "resources";
}

std::string_view PackPointKey(PackKind kind)
{
	switch (kind)
	{
	case PackKind::Throwable:
		return "grip";
	case PackKind::Weapon:
		return "tip";
	case PackKind::Resource:
	case PackKind::Spell:
		break;
	}
	return {};
}

std::optional<KnackValue> PackValue(const PackResource& item, std::string_view key)
{
	const auto found = std::ranges::find(item.values, key, &std::pair<std::string, KnackValue>::first);
	if (found == item.values.end())
	{
		return std::nullopt;
	}
	return found->second;
}

std::string SpellItemName(std::string_view power, int level)
{
	return std::format("{}{}", power, level);
}

std::span<const PackField> PackFields(PackKind kind)
{
	switch (kind)
	{
	case PackKind::Throwable:
		return k_ThrowableFields;
	case PackKind::Weapon:
		return k_WeaponFields;
	case PackKind::Spell:
		return k_SpellFields;
	case PackKind::Resource:
		break;
	}
	return {};
}

const PackField* FindPackField(PackKind kind, std::string_view key)
{
	const auto fields = PackFields(kind);
	const auto found = std::ranges::find(fields, key, &PackField::key);
	return found == fields.end() ? nullptr : &*found;
}

std::optional<std::string> NormalisePackPath(std::string_view text)
{
	std::string path(text);
	std::ranges::replace(path, '\\', '/');
	if (path.empty() || path.front() == '/' || path.find(':') != std::string::npos)
	{
		return std::nullopt;
	}
	size_t start = 0;
	while (start <= path.size())
	{
		const auto end = std::min(path.find('/', start), path.size());
		const auto part = std::string_view(path).substr(start, end - start);
		if (part.empty() || part == "." || part == "..")
		{
			return std::nullopt;
		}
		start = end + 1;
	}
	return path;
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b)
{
	return std::ranges::equal(a, b, [](char x, char y) { return ToLower(x) == ToLower(y); });
}

std::string DefaultResourceId(std::string_view packId, std::string_view resourceName)
{
	const auto text = std::format("{}/{}", packId, resourceName);
	const auto high = Fnv1a<uint64_t, 0xcbf29ce484222325ull, 0x100000001b3ull>(text);
	const auto low = Fnv1a<uint32_t, 0x811c9dc5u, 0x01000193u>(text);
	return std::format("{:016x}{:08x}", high, low);
}

moa::Result<Pack> ParsePack(std::string_view text)
{
	if (text.starts_with("\xEF\xBB\xBF"))
	{
		text.remove_prefix(3);
	}
	const auto json = Json::parse(text, nullptr, false);
	if (json.is_discarded())
	{
		return moa::MakeError("the text is not correct JSON");
	}
	if (!json.is_object())
	{
		return moa::MakeError("the file must contain a JSON object");
	}
	const auto format = json.find("format");
	if (format == json.end() || !format->is_number_integer())
	{
		return moa::MakeError("format: give the format number {} or {}", k_PackFormat, k_PackFormatTyped);
	}
	const auto formatNumber = format->get<int64_t>();
	if (formatNumber != k_PackFormat && formatNumber != k_PackFormatTyped)
	{
		return moa::MakeError("format: the format {} is not known; this mod reads the formats {} and {}", format->dump(),
		                      k_PackFormat, k_PackFormatTyped);
	}
	if (formatNumber == k_PackFormat)
	{
		if (auto keys = CheckKeys(json, "", {"format", "id", "name", "version", "description", "resources"}); !keys)
		{
			return keys.GetError();
		}
	}
	else if (auto keys = CheckKeys(
	             json, "", {"format", "id", "name", "version", "description", "resources", "throwables", "weapons", "spells"});
	         !keys)
	{
		return keys.GetError();
	}

	Pack pack;
	pack.format = static_cast<int>(formatNumber);
	auto id = ReadText(json, "", "id");
	if (!id)
	{
		return id.GetError();
	}
	if (!id->has_value() || !IsPackId(**id))
	{
		return moa::MakeError("id: the id must match [a-z0-9_-]{{1,32}}");
	}
	pack.id = std::move(**id);

	auto name = ReadText(json, "", "name");
	if (!name)
	{
		return name.GetError();
	}
	pack.name = name->value_or(pack.id);

	auto version = ReadText(json, "", "version");
	if (!version)
	{
		return version.GetError();
	}
	pack.version = version->value_or("");

	auto description = ReadText(json, "", "description");
	if (!description)
	{
		return description.GetError();
	}
	pack.description = description->value_or("");

	if (formatNumber == k_PackFormat && !json.contains("resources"))
	{
		return moa::MakeError("resources: give an array of resources");
	}
	for (const auto kind : k_Kinds)
	{
		const auto listName = std::string(PackListName(kind));
		const auto list = json.find(listName);
		if (list == json.end())
		{
			continue;
		}
		if (!list->is_array())
		{
			return moa::MakeError("{}: give an array", listName);
		}
		for (size_t i = 0; i < list->size(); ++i)
		{
			const auto path = std::format("{}[{}]", listName, i);
			auto resource = ReadResource((*list)[i], path, kind);
			if (!resource)
			{
				return resource.GetError();
			}
			for (const auto& other : pack.resources)
			{
				if (EqualsIgnoreCase(other.name, resource->name))
				{
					return moa::MakeError("{}.name: the pack has the name \"{}\" two times", path, other.name);
				}
				if (EqualsIgnoreCase(other.prefab, resource->prefab))
				{
					return moa::MakeError("{}.prefab: the pack has the prefab \"{}\" two times", path, other.prefab);
				}
				if (other.id && resource->id && *other.id == *resource->id)
				{
					return moa::MakeError("{}.id: the pack has the id \"{}\" two times", path, *other.id);
				}
			}
			pack.resources.push_back(std::move(*resource));
		}
	}
	return pack;
}

PackSet ParsePacks(std::span<const PackSource> sources)
{
	std::vector<const PackSource*> ordered;
	ordered.reserve(sources.size());
	for (const auto& source : sources)
	{
		ordered.push_back(&source);
	}
	std::ranges::sort(ordered, {}, &PackSource::folder);

	PackSet set;
	std::map<std::string, std::string> folderOfId;
	for (const auto* source : ordered)
	{
		auto pack = ParsePack(source->text);
		if (!pack)
		{
			set.errors.push_back({.pack = source->folder, .message = pack.GetError().message});
			continue;
		}
		if (const auto found = folderOfId.find(pack->id); found != folderOfId.end())
		{
			set.errors.push_back(
			    {.pack = source->folder,
			     .message = std::format("id: the folder \"{}\" has a pack with the same id \"{}\"", found->second, pack->id)});
			continue;
		}
		folderOfId.emplace(pack->id, source->folder);
		pack->folder = source->folder;
		set.packs.push_back(std::move(*pack));
	}
	std::ranges::sort(set.packs, {}, &Pack::id);
	return set;
}

} // namespace moa_mod::objects
