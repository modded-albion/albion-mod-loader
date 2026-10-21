/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <moa_mod/objects/CarriableMerge.h>
#include <nlohmann/json.hpp>

using namespace moa_mod::objects;

namespace
{

/// Three rows in the form of the game table: one line, the fields in the order of the game, all values text.
constexpr std::string_view k_Table =
    R"([{"id":"614c60231897000dd47e1ec4","m_name":"Build","m_title":"Build","m_prefab":"?","m_description":"","m_textSprite":"","m_sprite":"?","m_spriteColour":"#B76711FF","m_bezierColor":""},)"
    R"({"id":"67bd9f05fc0ab902fe097067","m_name":"Cotton","m_title":"Raw Cotton","m_prefab":"PickupCotton","m_description":"Soft \"white\" fibre","m_textSprite":"Cotton","m_sprite":"Cotton","m_spriteColour":"#378A3FFF","m_bezierColor":""},)"
    R"({"id":"679b625d5c6e4702dba72090","m_name":"Wood","m_title":"Wood","m_prefab":"PickupWood","m_description":"","m_textSprite":"Wood","m_sprite":"Wood","m_spriteColour":"#378A3FFF","m_bezierColor":""}])";

PackResource Resource(std::string name, std::string templateName)
{
	PackResource resource;
	resource.prefab = name;
	resource.name = std::move(name);
	resource.templateName = std::move(templateName);
	return resource;
}

Pack MakePack(std::string id, std::vector<PackResource> resources)
{
	return Pack {.folder = id, .id = id, .name = id, .version = "1", .description = {}, .resources = std::move(resources)};
}

} // namespace

TEST(ObjectsMerge, AddsRowsBeforeTheEndAndKeepsTheGameText)
{
	auto cotton = Resource("Custom_TestCotton", "Cotton");
	cotton.row = {{"m_title", "Test Cotton"}, {"m_spriteColour", "#B76711FF"}};
	auto wood = Resource("Custom_Log", "Wood");
	wood.id = "0123456789abcdef01234567";
	const std::vector packs {MakePack("example", {cotton, wood})};

	const std::string table = std::string("\xEF\xBB\xBF") + std::string(k_Table) + "\r\n";
	auto merge = MergeCarriableRows(table, packs);
	ASSERT_TRUE(merge) << merge.GetError().message;
	EXPECT_TRUE(merge->errors.empty());

	const auto end = table.rfind(']');
	EXPECT_EQ(merge->text.substr(0, end), table.substr(0, end));
	EXPECT_TRUE(merge->text.ends_with("]\r\n"));
	const auto id = DefaultResourceId("example", "Custom_TestCotton");
	const auto expected = std::format(
	    R"(,{{"id":"{}","m_name":"Custom_TestCotton","m_title":"Test Cotton","m_prefab":"Custom_TestCotton",)"
	    R"("m_description":"Soft \"white\" fibre","m_textSprite":"Cotton","m_sprite":"Cotton",)"
	    R"("m_spriteColour":"#B76711FF","m_bezierColor":""}})"
	    R"(,{{"id":"0123456789abcdef01234567","m_name":"Custom_Log","m_title":"Wood","m_prefab":"Custom_Log",)"
	    R"("m_description":"","m_textSprite":"Wood","m_sprite":"Wood","m_spriteColour":"#378A3FFF","m_bezierColor":""}})",
	    id);
	EXPECT_EQ(merge->text.substr(end, merge->text.size() - end - 3), expected);
	EXPECT_EQ(nlohmann::json::parse(merge->text.substr(3)).size(), 5u);

	ASSERT_EQ(merge->merged.size(), 2u);
	const auto& first = merge->merged[0];
	EXPECT_EQ(first.pack, 0u);
	EXPECT_EQ(first.resource, 0u);
	EXPECT_EQ(first.name, "Custom_TestCotton");
	EXPECT_EQ(first.id, id);
	EXPECT_EQ(first.prefabPath, "_Prefabs/RawMaterials/Custom_TestCotton");
	EXPECT_EQ(first.templatePrefabPath, "_Prefabs/RawMaterials/PickupCotton");
	ASSERT_EQ(first.row.size(), k_CarriableFields.size());
	for (size_t i = 0; i < k_CarriableFields.size(); ++i)
	{
		EXPECT_EQ(first.row[i].first, k_CarriableFields[i]);
	}
	EXPECT_EQ(merge->merged[1].resource, 1u);
	EXPECT_EQ(merge->merged[1].templatePrefabPath, "_Prefabs/RawMaterials/PickupWood");
}

TEST(ObjectsMerge, AddsToAnEmptyTableAndChangesNothingWithoutPacks)
{
	auto none = MergeCarriableRows(k_Table, {});
	ASSERT_TRUE(none) << none.GetError().message;
	EXPECT_EQ(none->text, k_Table);

	// An empty table has no template, so the pack does not get added.
	const std::vector packs {MakePack("p", {Resource("A", "Cotton")})};
	auto empty = MergeCarriableRows(" [ ] ", packs);
	ASSERT_TRUE(empty) << empty.GetError().message;
	EXPECT_EQ(empty->text, " [ ] ");
	ASSERT_EQ(empty->errors.size(), 1u);
}

TEST(ObjectsMerge, RefusesATableThatIsNotAnArray)
{
	EXPECT_FALSE(MergeCarriableRows("{}", {}));
	EXPECT_FALSE(MergeCarriableRows("[1, 2]", {}));
	EXPECT_FALSE(MergeCarriableRows("[{}", {}));
	EXPECT_FALSE(MergeCarriableRows("", {}));
}

TEST(ObjectsMerge, SkipsAPackWithAClashAndKeepsTheOthers)
{
	auto clashId = Resource("Fresh", "Cotton");
	clashId.id = "67bd9f05fc0ab902fe097067";
	auto clashPrefab = Resource("Fresh", "Cotton");
	clashPrefab.prefab = "pickupwood";
	const std::vector packs {
	    MakePack("a_good", {Resource("Custom_One", "Cotton")}),
	    MakePack("b_name", {Resource("Custom_Two", "Cotton"), Resource("COTTON", "Cotton")}),
	    MakePack("c_id", {clashId}),
	    MakePack("d_prefab", {clashPrefab}),
	    MakePack("e_pack", {Resource("custom_one", "Wood")}),
	    MakePack("f_template", {Resource("Custom_Three", "Missing")}),
	    MakePack("g_noprefab", {Resource("Custom_Four", "Build")}),
	    MakePack("h_good", {Resource("Custom_Two", "Wood")}),
	};
	auto merge = MergeCarriableRows(k_Table, packs);
	ASSERT_TRUE(merge) << merge.GetError().message;
	ASSERT_EQ(merge->merged.size(), 2u);
	EXPECT_EQ(merge->merged[0].name, "Custom_One");
	// The pack b_name does not add Custom_Two, so the pack h_good can add it.
	EXPECT_EQ(merge->merged[1].name, "Custom_Two");
	EXPECT_EQ(merge->merged[1].pack, 7u);
	ASSERT_EQ(merge->errors.size(), 6u);
	std::vector<std::string> failed;
	for (const auto& error : merge->errors)
	{
		failed.push_back(error.pack);
	}
	EXPECT_EQ(failed, (std::vector<std::string> {"b_name", "c_id", "d_prefab", "e_pack", "f_template", "g_noprefab"}));
	EXPECT_NE(merge->errors[0].message.find("COTTON"), std::string::npos);
	EXPECT_EQ(nlohmann::json::parse(merge->text).size(), 5u);
}

TEST(ObjectsMerge, MatchesTheResourcePathWithoutCase)
{
	const std::vector packs {MakePack("p", {Resource("Custom_One", "Cotton"), Resource("Custom_Two", "Wood")})};
	auto merge = MergeCarriableRows(k_Table, packs);
	ASSERT_TRUE(merge) << merge.GetError().message;
	const auto* found = MatchResourcePath("_prefabs/rawmaterials/custom_two", merge->merged);
	ASSERT_NE(found, nullptr);
	EXPECT_EQ(found->name, "Custom_Two");
	EXPECT_EQ(MatchResourcePath("_Prefabs\\RawMaterials\\Custom_One", merge->merged), &merge->merged[0]);
	EXPECT_EQ(MatchResourcePath("_Prefabs/RawMaterials/PickupCotton", merge->merged), nullptr);
	EXPECT_EQ(MatchResourcePath("_Prefabs/RawMaterials/Custom_On", merge->merged), nullptr);
}

TEST(ObjectsMerge, MergesIntoTheGameTableWhenItIsThere)
{
	const std::filesystem::path path = "D:/masters_of_albion/cache/custom-objects/new/NGCarriableResources.txt";
	std::ifstream file(path, std::ios::binary);
	if (!file)
	{
		GTEST_SKIP() << "The game table is not in the cache.";
	}
	const std::string table((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	auto cotton = Resource("Custom_TestCotton", "Cotton");
	cotton.row = {{"m_title", "Test Cotton"}};
	const std::vector packs {MakePack("example", {cotton})};
	auto merge = MergeCarriableRows(table, packs);
	ASSERT_TRUE(merge) << merge.GetError().message;
	ASSERT_TRUE(merge->errors.empty()) << merge->errors[0].message;
	const auto end = table.rfind(']');
	EXPECT_EQ(merge->text.substr(0, end), table.substr(0, end));
	auto body = std::string_view(merge->text);
	if (body.starts_with("\xEF\xBB\xBF"))
	{
		body.remove_prefix(3);
	}
	const auto rows = nlohmann::ordered_json::parse(body);
	const auto& added = rows.back();
	EXPECT_EQ(added["m_name"], "Custom_TestCotton");
	EXPECT_EQ(added["m_title"], "Test Cotton");
	std::vector<std::string> keys;
	for (const auto& [key, value] : added.items())
	{
		keys.push_back(key);
	}
	std::vector<std::string> gameKeys;
	for (const auto& [key, value] : rows.front().items())
	{
		gameKeys.push_back(key);
	}
	EXPECT_EQ(keys, gameKeys);
	EXPECT_EQ(merge->merged[0].templatePrefabPath, "_Prefabs/RawMaterials/PickupCotton");
}
