/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <moa_mod/objects/KnackMerge.h>
#include <nlohmann/json.hpp>

using namespace moa_mod::objects;

namespace
{

/// Two decoration rows in the form of the game table: numbers as text with two decimals, a price as a JSON number.
constexpr std::string_view k_Decorations =
    R"([{"id":"aaaaaaaaaaaaaaaaaaaaaaaa","m_name":"MA_Barrel","m_prefabName":"MA_Barrel","m_location":"_Art/Decorations/MA_Decorations","m_type":"Throwable","m_price":50,"m_canPickupAndThrow":"True","m_throwableMass":"200.00","m_health":""},)"
    R"({"id":"bbbbbbbbbbbbbbbbbbbbbbbb","m_name":"MA_Crate","m_prefabName":"MA_Crate","m_location":"_Art/Decorations/MA_Decorations","m_type":"Aesthetic","m_price":20,"m_canPickupAndThrow":"False","m_throwableMass":"","m_health":"10.00"}])";

/// Two levels of one hand power: the name is the same, the level is a JSON number.
constexpr std::string_view k_Powers =
    R"([{"id":"c1","m_name":"Fireball","m_level":1,"m_manaCost":25,"m_baseDamage":"10.00","m_title":"Fireball"},)"
    R"({"id":"c2","m_name":"Fireball","m_level":2,"m_manaCost":35,"m_baseDamage":"15.00","m_title":"Fireball"}])";

KnackRowRequest Request(std::string name, std::string templateName, std::vector<std::pair<std::string, KnackValue>> values)
{
	return KnackRowRequest {.name = std::move(name),
	                        .templateName = std::move(templateName),
	                        .values = std::move(values),
	                        .uniqueFields = {"id", "m_prefabName"}};
}

} // namespace

TEST(KnackMerge, FormatsValuesInTheFormOfTheTemplate)
{
	EXPECT_EQ(FormatKnackValue(7.5, R"("25.00")"), R"("7.50")");
	EXPECT_EQ(FormatKnackValue(0.125, R"("25.00")"), R"("0.125")");
	EXPECT_EQ(FormatKnackValue(3.0, R"("")"), R"("3.00")");
	EXPECT_EQ(FormatKnackValue(30.0, "25"), "30");
	EXPECT_EQ(FormatKnackValue(2.5, "25"), "2.5");
	EXPECT_EQ(FormatKnackValue(5.0, R"("5")"), R"("5")");
	EXPECT_EQ(FormatKnackValue(true, R"("False")"), R"("True")");
	EXPECT_EQ(FormatKnackValue(false, "true"), "false");
	EXPECT_EQ(FormatKnackValue(true, "0"), "1");
	EXPECT_EQ(FormatKnackValue(std::string("a \"b\""), "1"), R"("a \"b\"")");
}

TEST(KnackMerge, CopiesTheTemplateRowAndKeepsTheGameText)
{
	const std::vector packs {KnackPackRows {.pack = "rocks",
	                                        .rows = {Request("MOAR_Rock", "MA_Barrel",
	                                                         {{"id", std::string("0123456789abcdef01234567")},
	                                                          {"m_prefabName", std::string("MOAR_Rock")},
	                                                          {"m_location", std::string("_MOAR/Throwables")},
	                                                          {"m_price", 75.0},
	                                                          {"m_throwableMass", 320.0},
	                                                          {"m_health", 40.0}})}}};
	const std::string table = std::string("\xEF\xBB\xBF") + std::string(k_Decorations) + "\n";
	auto merge = MergeKnackRows(table, packs);
	ASSERT_TRUE(merge) << merge.GetError().message;
	ASSERT_TRUE(merge->errors.empty());
	ASSERT_EQ(merge->added.size(), 1u);

	const auto end = table.rfind(']');
	EXPECT_EQ(merge->text.substr(0, end), table.substr(0, end));
	EXPECT_TRUE(merge->text.ends_with("]\n"));
	const auto expected =
	    R"({"id":"0123456789abcdef01234567","m_name":"MOAR_Rock","m_prefabName":"MOAR_Rock","m_location":"_MOAR/Throwables","m_type":"Throwable","m_price":75,"m_canPickupAndThrow":"True","m_throwableMass":"320.00","m_health":"40.00"})";
	EXPECT_NE(merge->text.find(std::string(",") + expected + "]"), std::string::npos) << merge->text;
	EXPECT_EQ(KnackFieldText(merge->added[0], "m_location"), "_MOAR/Throwables");
	EXPECT_EQ(KnackField(merge->added[0], "m_price"), "75");

	// The game can read the result.
	const auto json = nlohmann::json::parse(merge->text.substr(3));
	ASSERT_EQ(json.size(), 3u);
	EXPECT_EQ(json[2]["m_throwableMass"], "320.00");
}

TEST(KnackMerge, RefusesAWholePackOnAClash)
{
	const std::vector packs {
	    KnackPackRows {
	        .pack = "first",
	        .rows = {Request("MOAR_A", "MA_Barrel", {{"id", std::string("1")}, {"m_prefabName", std::string("A")}})}},
	    KnackPackRows {
	        .pack = "second",
	        .rows = {Request("MOAR_B", "MA_Barrel", {{"id", std::string("2")}, {"m_prefabName", std::string("B")}}),
	                 Request("moar_a", "MA_Barrel", {{"id", std::string("3")}, {"m_prefabName", std::string("C")}})}},
	    KnackPackRows {
	        .pack = "third",
	        .rows = {Request("MOAR_D", "MA_Barrel", {{"id", std::string("4")}, {"m_prefabName", std::string("ma_crate")}})}},
	    KnackPackRows {
	        .pack = "fourth",
	        .rows = {Request("MOAR_E", "NoSuchRow", {{"id", std::string("5")}, {"m_prefabName", std::string("E")}})}},
	    KnackPackRows {
	        .pack = "fifth",
	        .rows = {Request("MA_Crate", "MA_Barrel", {{"id", std::string("6")}, {"m_prefabName", std::string("F")}})}},
	};
	auto merge = MergeKnackRows(k_Decorations, packs);
	ASSERT_TRUE(merge) << merge.GetError().message;
	ASSERT_EQ(merge->added.size(), 1u);
	EXPECT_EQ(merge->added[0].name, "MOAR_A");
	ASSERT_EQ(merge->errors.size(), 4u);
	EXPECT_EQ(merge->errors[0].first, "second");
	EXPECT_NE(merge->errors[0].second.find("pack \"first\""), std::string::npos) << merge->errors[0].second;
	EXPECT_NE(merge->errors[1].second.find("m_prefabName"), std::string::npos) << merge->errors[1].second;
	EXPECT_NE(merge->errors[2].second.find("not a row of the game"), std::string::npos) << merge->errors[2].second;
	EXPECT_NE(merge->errors[3].second.find("game row"), std::string::npos) << merge->errors[3].second;
}

TEST(KnackMerge, AddsALevelToAPowerWhenTheRulesAllowIt)
{
	const KnackTableRules rules {.keyField = "m_name", .keyIgnoresCase = false, .levelField = "m_level"};
	const auto request = [](std::string name, double level, std::string id) {
		return KnackRowRequest {.name = std::move(name),
		                        .templateName = "Fireball",
		                        .values = {{"id", std::move(id)}, {"m_level", level}, {"m_manaCost", 40.0}},
		                        .uniqueFields = {"id"}};
	};
	const std::vector packs {
	    KnackPackRows {.pack = "new-level", .rows = {request("Fireball", 6.0, "n1")}},
	    KnackPackRows {.pack = "same-level", .rows = {request("Fireball", 2.0, "n2")}},
	    KnackPackRows {.pack = "new-name", .rows = {request("Firestorm", 1.0, "n3")}},
	};
	auto merge = MergeKnackRows(k_Powers, packs, rules);
	ASSERT_TRUE(merge) << merge.GetError().message;
	ASSERT_EQ(merge->added.size(), 2u);
	EXPECT_EQ(KnackField(merge->added[0], "m_level"), "6");
	EXPECT_EQ(KnackField(merge->added[0], "m_manaCost"), "40");
	EXPECT_EQ(merge->added[1].name, "Firestorm");
	ASSERT_EQ(merge->errors.size(), 1u);
	EXPECT_EQ(merge->errors[0].first, "same-level");
}

TEST(KnackMerge, RefusesATextThatIsNotAnArray)
{
	EXPECT_FALSE(MergeKnackRows("{}", {}));
	EXPECT_FALSE(MergeKnackRows("[1, 2]", {}));
	EXPECT_FALSE(MergeKnackRows("not json", {}));
	auto empty = MergeKnackRows("[]", {});
	ASSERT_TRUE(empty);
	EXPECT_EQ(empty->text, "[]");
}

TEST(KnackMerge, KeepsTheDecimalsOfTheTemplate)
{
	EXPECT_EQ(FormatKnackValue(0.6, R"("0.47000")"), R"("0.60000")");
	EXPECT_EQ(FormatKnackValue(12.0, R"("12.5")"), R"("12.0")");
}

TEST(KnackMerge, PatchesOnlyTheBytesOfTheChangedValues)
{
	const std::string table = "\xEF\xBB\xBF[ {\"m_key\":\"A\", \"m_link\":\"\" , \"n\":{\"x\":[1,\"]\"]}},\n"
	                          "{\"m_key\":\"B\",\"m_link\":\"A|C\",\"v\":2.50} ]\r\n";
	const std::vector<KnackFieldPatch> patches {{.key = "B", .field = "m_link", .jsonValue = R"("A|C|D")"},
	                                            {.key = "A", .field = "m_link", .jsonValue = R"("B")"}};
	auto patched = PatchKnackFields(table, patches, "m_key");
	ASSERT_TRUE(patched) << patched.GetError().message;
	EXPECT_EQ(*patched, "\xEF\xBB\xBF[ {\"m_key\":\"A\", \"m_link\":\"B\" , \"n\":{\"x\":[1,\"]\"]}},\n"
	                    "{\"m_key\":\"B\",\"m_link\":\"A|C|D\",\"v\":2.50} ]\r\n");

	const std::vector<KnackFieldPatch> missingRow {{.key = "Z", .field = "m_link", .jsonValue = R"("")"}};
	EXPECT_FALSE(PatchKnackFields(table, missingRow, "m_key"));
	const std::vector<KnackFieldPatch> missingField {{.key = "A", .field = "m_none", .jsonValue = R"("")"}};
	EXPECT_FALSE(PatchKnackFields(table, missingField, "m_key"));
	const std::vector<KnackFieldPatch> badValue {{.key = "A", .field = "m_link", .jsonValue = "not json"}};
	EXPECT_FALSE(PatchKnackFields(table, badValue, "m_key"));
	EXPECT_FALSE(PatchKnackFields("[{\"m_key\":\"A\"", {}, "m_key"));
}
