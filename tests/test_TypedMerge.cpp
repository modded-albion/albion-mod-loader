/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <core/TypedMerge.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace moa_mod::objects;

namespace
{

constexpr std::string_view k_Decorations =
    R"([{"id":"aaaaaaaaaaaaaaaaaaaaaaaa","m_name":"MA_Throwable_Boulder1","m_prefabName":"MA_Throwable_Boulder1","m_location":"_Art/Decorations/Throwable","m_type":"Throwable","m_price":0,"m_starterPack":"False","m_isOnDecorationStrip":"False","m_displayName":"Boulder","m_throwExplosionDamageBase":"50.00","m_throwExplosionSpeedBase":"6.00","m_throwableMass":"200.00","m_maxImpactCount":"","m_health":"25.00"}])";

constexpr std::string_view k_Blocks =
    R"([{"id":"bbbbbbbbbbbbbbbbbbbbbbbb","m_prefabName":"Weapon_Blade_Sword_A","m_displayName":"Shortsword Blade","m_nGProductInfo":"Weapons","m_mADrawerInfos":"Weapons:Blades","m_tags":"Blade|Weapon|Weapon_Tip","m_price":"8.30","m_weight":"12.50","m_starterPack":"True","m_prefabPath":"_CurrentlyUsed/_MA_Weapons/Weapon_Blade_Sword_A","m_attack":"0.47000","m_tooltip":"Shortsword Blade"},)"
    R"({"id":"cccccccccccccccccccccccc","m_prefabName":"Factory_Input","m_nGProductInfo":"","m_prefabPath":"_CurrentlyUsed/_MA_Factory/Factory_Input","m_attack":"0.00000"}])";

constexpr std::string_view k_Powers =
    R"([{"id":"p4","m_indexer":"Fireball4","m_name":"Fireball","m_level":4,"m_manaCost":45,"m_baseDamage":"20.00","m_title":"Fireball"},)"
    R"({"id":"p5","m_indexer":"Fireball5","m_name":"Fireball","m_level":5,"m_manaCost":50,"m_baseDamage":"25.00","m_title":"Fireball"},)"
    R"({"id":"d5","m_indexer":"Dig5","m_name":"Dig","m_level":5,"m_manaCost":5,"m_baseDamage":"","m_title":"Dig"}])";

constexpr std::string_view k_Research =
    "[{\"id\":\"r4\",\"m_key\":\"Mystic::HandPowerFireballL4\",\"m_name\":\"HandPowerFireballL4\",\"m_linkToString\":"
    "\"Mystic::HandPowerFireballL5\",\"m_unlock\":\"m_handPowerFireballLevel=4\",\"m_dollarCost\":20000,\"m_icon\":"
    "\"ARC_fireball\",\"m_title\":\"Fireball Lvl 4\",\"m_description\":\"\",\"m_position\":\"(-575.87, 383.27, 0.00)\","
    "\"m_activatedAtStart\":\"False\",\"m_newDisplayName\":\"Fireball Lvl 4\",\"m_newDescription\":\"\","
    "\"m_newDetailsLine\":\"Base Damage:MAHandPowerInfo[Fireball4].m_baseDamage\"},\n"
    "{\"id\":\"r5\",\"m_key\":\"Mystic::HandPowerFireballL5\",\"m_name\":\"HandPowerFireballL5\",\"m_linkToString\":\"\","
    "\"m_unlock\":\"m_handPowerFireballLevel=5\",\"m_dollarCost\":25000,\"m_icon\":\"ARC_fireball\",\"m_title\":"
    "\"Fireball Lvl 5\",\"m_description\":\"\",\"m_position\":\"(-919.39, 321.96, 0.00)\",\"m_activatedAtStart\":"
    "\"False\",\"m_newDisplayName\":\"Fireball Lvl 5\",\"m_newDescription\":\"\",\"m_newDetailsLine\":"
    "\"Base Damage:MAHandPowerInfo[Fireball5].m_baseDamage|Mana:MAHandPowerInfo[Fireball5].m_manaCost\"}]";

PackResource Item(PackKind kind, std::string name, std::string templateName,
                  std::vector<std::pair<std::string, KnackValue>> values = {})
{
	PackResource item;
	item.kind = kind;
	item.prefab = name;
	item.name = std::move(name);
	item.templateName = std::move(templateName);
	item.values = std::move(values);
	return item;
}

PackResource Spell(std::string power, int level, std::vector<std::pair<std::string, KnackValue>> values = {})
{
	values.insert(values.begin(), {"level", static_cast<double>(level)});
	auto name = SpellItemName(power, level);
	return Item(PackKind::Spell, std::move(name), std::move(power), std::move(values));
}

Pack MakePack(std::string id, std::vector<PackResource> items)
{
	return Pack {.folder = id, .id = id, .name = id, .version = "1", .description = {}, .resources = std::move(items)};
}

[[nodiscard]] nlohmann::json Parse(const std::string& text)
{
	return nlohmann::json::parse(text);
}

} // namespace

TEST(TypedMerge, AddsThrowablesWithTheirPrefabPaths)
{
	const std::vector packs {MakePack(
	    "rocks", {Item(PackKind::Resource, "Ignored", "Wood"),
	              Item(PackKind::Throwable, "MOAR_Rock", "MA_Throwable_Boulder1",
	                   {{"title", std::string("Rock")}, {"mass", 300.0}, {"maxImpacts", 2.0}, {"onDecorationStrip", true}})})};
	auto merge = MergeThrowableRows(k_Decorations, packs);
	ASSERT_TRUE(merge) << merge.GetError().message;
	ASSERT_TRUE(merge->errors.empty()) << merge->errors[0].message;
	ASSERT_EQ(merge->merged.size(), 1u);
	const auto& rock = merge->merged[0];
	EXPECT_EQ(rock.resource, 1u);
	EXPECT_EQ(rock.prefabPath, "_Art/Decorations/MOAR/MOAR_Rock");
	EXPECT_EQ(rock.templatePrefabPath, "_Art/Decorations/Throwable/MA_Throwable_Boulder1");
	EXPECT_EQ(rock.id, DefaultResourceId("rocks", "MOAR_Rock"));

	const auto table = Parse(merge->text);
	ASSERT_EQ(table.size(), 2u);
	const auto& row = table[1];
	EXPECT_EQ(row["m_name"], "MOAR_Rock");
	EXPECT_EQ(row["m_prefabName"], "MOAR_Rock");
	EXPECT_EQ(row["m_location"], "_Art/Decorations/MOAR");
	EXPECT_EQ(row["m_displayName"], "Rock");
	EXPECT_EQ(row["m_throwableMass"], "300.00");
	EXPECT_EQ(row["m_maxImpactCount"], "2.00");
	EXPECT_EQ(row["m_isOnDecorationStrip"], "True");
	EXPECT_EQ(row["m_throwExplosionSpeedBase"], "6.00");
}

TEST(TypedMerge, AddsWeaponPartsAndRefusesOtherBlocks)
{
	const std::vector packs {
	    MakePack("blades", {Item(PackKind::Weapon, "MOAR_Blade", "Weapon_Blade_Sword_A",
	                             {{"title", std::string("Moon Blade")}, {"attack", 0.6}, {"weight", 14.0}})}),
	    MakePack("bad", {Item(PackKind::Weapon, "MOAR_Wall", "Factory_Input")}),
	};
	auto merge = MergeWeaponRows(k_Blocks, packs);
	ASSERT_TRUE(merge) << merge.GetError().message;
	ASSERT_EQ(merge->errors.size(), 1u);
	EXPECT_EQ(merge->errors[0].pack, "bad");
	ASSERT_EQ(merge->merged.size(), 1u);
	const auto& blade = merge->merged[0];
	EXPECT_EQ(blade.prefabPath, "_Prefabs/_Blocks/MOAR/blades/MOAR_Blade");
	EXPECT_EQ(blade.templatePrefabPath, "_Prefabs/_Blocks/_CurrentlyUsed/_MA_Weapons/Weapon_Blade_Sword_A");

	const auto table = Parse(merge->text);
	ASSERT_EQ(table.size(), 3u);
	const auto& row = table[2];
	EXPECT_EQ(row["m_prefabName"], "MOAR_Blade");
	EXPECT_EQ(row["m_prefabPath"], "MOAR/blades/MOAR_Blade");
	EXPECT_EQ(row["m_displayName"], "Moon Blade");
	EXPECT_EQ(row["m_tooltip"], "Moon Blade");
	EXPECT_EQ(row["m_attack"], "0.60000");
	EXPECT_EQ(row["m_weight"], "14.00");
	EXPECT_EQ(row["m_mADrawerInfos"], "Weapons:Blades");
}

TEST(TypedMerge, AddsTheNextLevelsOfAPowerAndLinksTheResearch)
{
	const std::vector packs {
	    MakePack("fire", {Spell("Fireball", 7, {{"manaCost", 70.0}}), Spell("Fireball", 6,
	                                                                        {{"manaCost", 60.0},
	                                                                         {"baseDamage", 30.0},
	                                                                         {"title", std::string("Fireball")},
	                                                                         {"researchTitle", std::string("Fireball Lvl 6")},
	                                                                         {"dollarCost", 30000.0}})})};
	auto merge = MergeSpellRows(k_Powers, k_Research, packs);
	ASSERT_TRUE(merge) << merge.GetError().message;
	ASSERT_TRUE(merge->errors.empty()) << merge->errors[0].message;
	ASSERT_EQ(merge->added.size(), 2u);
	EXPECT_EQ(merge->added[0].level, 6);
	EXPECT_EQ(merge->added[0].researchKey, "Mystic::HandPowerFireballL6");
	EXPECT_EQ(merge->added[0].unlock, "m_handPowerFireballLevel=6");
	EXPECT_EQ(merge->added[1].level, 7);

	const auto powers = Parse(merge->handPowers);
	ASSERT_EQ(powers.size(), 5u);
	EXPECT_EQ(powers[3]["m_name"], "Fireball");
	EXPECT_EQ(powers[3]["m_level"], 6);
	EXPECT_EQ(powers[3]["m_indexer"], "Fireball6");
	EXPECT_EQ(powers[3]["m_manaCost"], 60);
	EXPECT_EQ(powers[3]["m_baseDamage"], "30.00");
	EXPECT_EQ(powers[4]["m_level"], 7);
	EXPECT_EQ(powers[4]["m_baseDamage"], "25.00"); // A copy of the level 5 of the game.

	// The level 5 row links to level 6; only that value changes in the original text.
	EXPECT_NE(merge->research.find("\"m_linkToString\":\"Mystic::HandPowerFireballL6\""), std::string::npos);
	const auto research = Parse(merge->research);
	ASSERT_EQ(research.size(), 4u);
	const auto& six = research[2];
	EXPECT_EQ(six["m_key"], "Mystic::HandPowerFireballL6");
	EXPECT_EQ(six["m_name"], "HandPowerFireballL6");
	EXPECT_EQ(six["m_unlock"], "m_handPowerFireballLevel=6");
	EXPECT_EQ(six["m_linkToString"], "Mystic::HandPowerFireballL7");
	EXPECT_EQ(six["m_title"], "Fireball Lvl 6");
	EXPECT_EQ(six["m_dollarCost"], 30000);
	EXPECT_EQ(six["m_activatedAtStart"], "False");
	EXPECT_EQ(six["m_newDetailsLine"],
	          "Base Damage:MAHandPowerInfo[Fireball6].m_baseDamage|Mana:MAHandPowerInfo[Fireball6].m_manaCost");
	EXPECT_EQ(six["m_position"], "(-1262.91, 260.65, 0.00)");
	EXPECT_EQ(research[3]["m_linkToString"], "");
	EXPECT_EQ(research[3]["m_title"], "Fireball Lvl 7");
	EXPECT_EQ(research[3]["m_position"], "(-1606.43, 199.34, 0.00)");
}

TEST(TypedMerge, RefusesAGapAndLinksTheLevelsOfTwoPacks)
{
	const std::vector packs {
	    MakePack("gap", {Spell("Fireball", 7)}),
	    MakePack("dig", {Spell("Dig", 6)}),
	    MakePack("good", {Spell("Fireball", 6)}),
	    MakePack("later", {Spell("Fireball", 7, {{"templateLevel", 4.0}})}),
	};
	auto merge = MergeSpellRows(k_Powers, k_Research, packs);
	ASSERT_TRUE(merge) << merge.GetError().message;
	ASSERT_EQ(merge->errors.size(), 2u);
	EXPECT_EQ(merge->errors[0].pack, "gap");
	EXPECT_EQ(merge->errors[1].pack, "dig");
	// The level 7 of a later pack follows the level 6 of the pack before it, and its research row links to it.
	ASSERT_EQ(merge->added.size(), 2u);
	EXPECT_EQ(merge->added[0].level, 6);
	EXPECT_EQ(merge->added[1].level, 7);
	const auto research = Parse(merge->research);
	ASSERT_EQ(research.size(), 4u);
	EXPECT_EQ(research[2]["m_linkToString"], "Mystic::HandPowerFireballL7");
	const auto powers = Parse(merge->handPowers);
	ASSERT_EQ(powers.size(), 5u);
	EXPECT_EQ(powers[4]["m_baseDamage"], "20.00"); // The template level 4.
}

namespace
{
/// Read a table of the game from the cache of the research, or return nothing.
std::optional<std::string> CachedTable(std::string_view name)
{
	const auto path = std::filesystem::path("D:/masters_of_albion/cache/custom-objects/new") / std::format("{}.txt", name);
	std::ifstream file(path, std::ios::binary);
	if (!file)
	{
		return std::nullopt;
	}
	return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}
} // namespace

TEST(TypedMerge, MergesIntoTheGameTablesWhenTheyAreThere)
{
	const auto decorations = CachedTable("NGDecorationInfo");
	const auto blocks = CachedTable("NGBlockInfo");
	const auto powers = CachedTable("MAHandPowerInfo");
	const auto research = CachedTable("MAResearchInfo");
	if (!decorations || !blocks || !powers || !research)
	{
		GTEST_SKIP() << "The game tables are not in the cache.";
	}
	const std::vector packs {
	    MakePack("proof", {Item(PackKind::Throwable, "MOAR_Rock", "MA_Throwable_Boulder1", {{"mass", 300.0}}),
	                       Item(PackKind::Weapon, "MOAR_Blade", "Weapon_Blade_Sword_A", {{"attack", 0.6}}),
	                       Spell("Fireball", 6, {{"manaCost", 70.0}}), Spell("Lightning", 6)})};

	auto throwables = MergeThrowableRows(*decorations, packs);
	ASSERT_TRUE(throwables) << throwables.GetError().message;
	ASSERT_TRUE(throwables->errors.empty()) << throwables->errors[0].message;
	EXPECT_EQ(throwables->merged.at(0).templatePrefabPath, "_Art/Decorations/Throwable/MA_Throwable_Boulder1");

	auto weapons = MergeWeaponRows(*blocks, packs);
	ASSERT_TRUE(weapons) << weapons.GetError().message;
	ASSERT_TRUE(weapons->errors.empty()) << weapons->errors[0].message;
	EXPECT_EQ(weapons->merged.at(0).templatePrefabPath, "_Prefabs/_Blocks/_CurrentlyUsed/_MA_Weapons/Weapon_Blade_Sword_A");
	EXPECT_EQ(KnackValue(std::string(Parse(weapons->text).back()["m_attack"])), KnackValue(std::string("0.60000")));

	auto spells = MergeSpellRows(*powers, *research, packs);
	ASSERT_TRUE(spells) << spells.GetError().message;
	ASSERT_TRUE(spells->errors.empty()) << spells->errors[0].message;
	ASSERT_EQ(spells->added.size(), 2u);
	EXPECT_EQ(spells->added[0].researchKey, "Mystic::HandPowerFireballL6");
	// The key of the research rows of the lightning has another form; the merge keeps it.
	EXPECT_EQ(spells->added[1].unlock, "m_handPowerLightningLevel=6");
	EXPECT_TRUE(spells->added[1].researchKey.ends_with("L6")) << spells->added[1].researchKey;
	const auto researchRows = Parse(spells->research);
	EXPECT_EQ(researchRows.size(), Parse(*research).size() + 2);
	// The level 5 rows link to the new rows.
	EXPECT_NE(spells->research.find("Mystic::HandPowerFireballL6"), std::string::npos);
	const auto powerRows = Parse(spells->handPowers);
	EXPECT_EQ(powerRows.size(), Parse(*powers).size() + 2);
}
