/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <format>
#include <string>
#include <vector>

#include <core/Pack.h>
#include <core/PackWriter.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace moa_mod::objects;

namespace
{

constexpr std::string_view k_FullPack = R"({
  "format": 1, "id": "example", "name": "Example pack", "version": "1.0.0",
  "resources": [
    {"name": "Custom_TestCotton", "template": "Cotton", "model": "models\\test_cotton.glb",
     "scale": 0.5, "pivot": [1, 2.5, -3], "collider": "box",
     "row": {"m_title": "Test Cotton", "m_spriteColour": "#B76711FF", "m_bezierColor": "#ff00ff"}},
    {"name": "Custom_Plain", "template": "Wood", "prefab": "PickupPlain", "id": "0123456789abcdef01234567"}
  ]})";

/// Make a pack text with one resource that has the given extra JSON text.
std::string PackWithResource(std::string_view extra)
{
	return std::format(R"({{"format": 1, "id": "p", "resources": [{{"name": "Thing", "template": "Cotton"{}}}]}})", extra);
}

/// Parse a pack text and return the error message, or an empty text if the parse worked.
std::string ErrorOf(std::string_view text)
{
	auto pack = ParsePack(text);
	return pack ? std::string {} : pack.GetError().message;
}

} // namespace

TEST(ObjectsPack, ReadsAllTheFields)
{
	auto pack = ParsePack(k_FullPack);
	ASSERT_TRUE(pack) << pack.GetError().message;
	EXPECT_EQ(pack->id, "example");
	EXPECT_EQ(pack->name, "Example pack");
	EXPECT_EQ(pack->version, "1.0.0");
	ASSERT_EQ(pack->resources.size(), 2u);

	const auto& cotton = pack->resources[0];
	EXPECT_EQ(cotton.name, "Custom_TestCotton");
	EXPECT_EQ(cotton.templateName, "Cotton");
	EXPECT_EQ(cotton.prefab, "Custom_TestCotton");
	EXPECT_FALSE(cotton.id);
	EXPECT_EQ(cotton.model, "models/test_cotton.glb");
	EXPECT_FLOAT_EQ(cotton.scale, 0.5f);
	EXPECT_EQ(cotton.pivot, PivotMode::Explicit);
	EXPECT_EQ(cotton.pivotPoint, (std::array {1.0f, 2.5f, -3.0f}));
	EXPECT_EQ(cotton.collider, ColliderMode::Box);
	ASSERT_EQ(cotton.row.size(), 3u);
	EXPECT_EQ(cotton.row[0], (std::pair<std::string, std::string> {"m_bezierColor", "#ff00ff"}));

	const auto& plain = pack->resources[1];
	EXPECT_EQ(plain.prefab, "PickupPlain");
	EXPECT_EQ(plain.id, "0123456789abcdef01234567");
	EXPECT_FALSE(plain.model);
	EXPECT_FLOAT_EQ(plain.scale, 1.0f);
	EXPECT_EQ(plain.pivot, PivotMode::BottomCentre);
	EXPECT_EQ(plain.collider, ColliderMode::Template);
	EXPECT_TRUE(plain.row.empty());
}

TEST(ObjectsPack, ReadsTheDefaultsAndAByteOrderMark)
{
	auto pack = ParsePack("\xEF\xBB\xBF{\"format\": 1, \"id\": \"a-b_9\", \"resources\": []}");
	ASSERT_TRUE(pack) << pack.GetError().message;
	EXPECT_EQ(pack->name, "a-b_9");
	EXPECT_EQ(pack->version, "");
	EXPECT_TRUE(pack->resources.empty());

	for (const auto* pivot : {"bottom-centre", "centre", "origin"})
	{
		auto one = ParsePack(PackWithResource(std::format(R"(, "pivot": "{}")", pivot)));
		ASSERT_TRUE(one) << one.GetError().message;
	}
}

TEST(ObjectsPack, RefusesBadFilesWithThePathOfTheField)
{
	EXPECT_EQ(ErrorOf("not json"), "the text is not correct JSON");
	EXPECT_EQ(ErrorOf("[]"), "the file must contain a JSON object");
	EXPECT_TRUE(ErrorOf(R"({"format": 3, "id": "p", "resources": []})").starts_with("format: the format 3"));
	EXPECT_TRUE(ErrorOf(R"({"id": "p", "resources": []})").starts_with("format:"));
	EXPECT_TRUE(ErrorOf(R"({"format": 1, "id": "Bad", "resources": []})").starts_with("id:"));
	EXPECT_TRUE(ErrorOf(R"({"format": 1, "id": "", "resources": []})").starts_with("id:"));
	EXPECT_TRUE(ErrorOf(R"({"format": 1, "id": "abcdefghijklmnopqrstuvwxyz0123456", "resources": []})").starts_with("id:"));
	EXPECT_TRUE(ErrorOf(R"({"format": 1, "id": "p"})").starts_with("resources:"));
	EXPECT_TRUE(ErrorOf(R"({"format": 1, "id": "p", "resources": [], "extra": 1})").starts_with("extra:"));

	EXPECT_TRUE(
	    ErrorOf(R"({"format": 1, "id": "p", "resources": [{"template": "Cotton"}]})").starts_with("resources[0].name:"));
	EXPECT_TRUE(ErrorOf(R"({"format": 1, "id": "p", "resources": [{"name": "9x", "template": "Cotton"}]})")
	                .starts_with("resources[0].name:"));
	EXPECT_TRUE(ErrorOf(R"({"format": 1, "id": "p", "resources": [{"name": "a-b", "template": "Cotton"}]})")
	                .starts_with("resources[0].name:"));
	EXPECT_TRUE(ErrorOf(R"({"format": 1, "id": "p", "resources": [{"name": "Thing"}]})").starts_with("resources[0].template:"));

	EXPECT_TRUE(ErrorOf(PackWithResource(R"(, "colour": 1)")).starts_with("resources[0].colour:"));
	EXPECT_TRUE(ErrorOf(PackWithResource(R"(, "prefab": "a/b")")).starts_with("resources[0].prefab:"));
	EXPECT_TRUE(ErrorOf(PackWithResource(R"(, "id": "0123456789ABCDEF01234567")")).starts_with("resources[0].id:"));
	EXPECT_TRUE(ErrorOf(PackWithResource(R"(, "id": "0123")")).starts_with("resources[0].id:"));
	EXPECT_TRUE(ErrorOf(PackWithResource(R"(, "scale": 0)")).starts_with("resources[0].scale:"));
	EXPECT_TRUE(ErrorOf(PackWithResource(R"(, "scale": "1")")).starts_with("resources[0].scale:"));
	EXPECT_TRUE(ErrorOf(PackWithResource(R"(, "pivot": "top")")).starts_with("resources[0].pivot:"));
	EXPECT_TRUE(ErrorOf(PackWithResource(R"(, "pivot": [1, 2])")).starts_with("resources[0].pivot:"));
	EXPECT_TRUE(ErrorOf(PackWithResource(R"(, "pivot": [1, "a", 2])")).starts_with("resources[0].pivot[1]:"));
	EXPECT_TRUE(ErrorOf(PackWithResource(R"(, "collider": "sphere")")).starts_with("resources[0].collider:"));
	EXPECT_TRUE(ErrorOf(PackWithResource(R"(, "row": [])")).starts_with("resources[0].row:"));
	EXPECT_TRUE(ErrorOf(PackWithResource(R"(, "row": {"m_cost": "1"})")).starts_with("resources[0].row.m_cost:"));
	EXPECT_TRUE(ErrorOf(PackWithResource(R"(, "row": {"m_name": "X"})")).starts_with("resources[0].row.m_name:"));
	EXPECT_TRUE(ErrorOf(PackWithResource(R"(, "row": {"m_prefab": "X"})")).starts_with("resources[0].row.m_prefab:"));
	EXPECT_TRUE(ErrorOf(PackWithResource(R"(, "row": {"id": "X"})")).starts_with("resources[0].row.id:"));
	EXPECT_TRUE(ErrorOf(PackWithResource(R"(, "row": {"m_title": 5})")).starts_with("resources[0].row.m_title:"));
	EXPECT_TRUE(
	    ErrorOf(PackWithResource(R"(, "row": {"m_spriteColour": "#FFF"})")).starts_with("resources[0].row.m_spriteColour:"));
	EXPECT_TRUE(
	    ErrorOf(PackWithResource(R"(, "row": {"m_bezierColor": "red"})")).starts_with("resources[0].row.m_bezierColor:"));
	EXPECT_EQ(ErrorOf(PackWithResource(R"(, "row": {"m_spriteColour": ""})")), "");
}

TEST(ObjectsPack, KeepsTheModelInThePackFolder)
{
	for (const auto* path :
	     {"../x.glb", "a/../../x.glb", "/x.glb", "\\\\server\\x.glb", "C:/x.glb", "a//b.glb", "./x.glb", "", "a/"})
	{
		EXPECT_TRUE(ErrorOf(PackWithResource(std::format(R"(, "model": {})", nlohmann::json(path).dump())))
		                .starts_with("resources[0].model:"))
		    << path;
	}
	EXPECT_EQ(ErrorOf(PackWithResource(R"(, "model": "a/b..c/x.glb")")), "");
}

TEST(ObjectsPack, RefusesTheSameResourceTwoTimes)
{
	const auto twice = [](std::string_view a, std::string_view b) {
		return std::format(
		    R"({{"format": 1, "id": "p", "resources": [{{"template": "Cotton", {}}}, {{"template": "Cotton", {}}}]}})", a, b);
	};
	EXPECT_TRUE(ErrorOf(twice(R"("name": "Thing")", R"("name": "THING")")).starts_with("resources[1].name:"));
	EXPECT_TRUE(
	    ErrorOf(twice(R"("name": "A", "prefab": "P")", R"("name": "B", "prefab": "p")")).starts_with("resources[1].prefab:"));
	EXPECT_TRUE(
	    ErrorOf(twice(R"("name": "A", "id": "0123456789abcdef01234567")", R"("name": "B", "id": "0123456789abcdef01234567")"))
	        .starts_with("resources[1].id:"));
}

TEST(ObjectsPack, MakesAStableDefaultId)
{
	const auto id = DefaultResourceId("example", "Custom_TestCotton");
	EXPECT_EQ(id.size(), 24u);
	EXPECT_EQ(id, DefaultResourceId("example", "Custom_TestCotton"));
	EXPECT_NE(id, DefaultResourceId("example", "Custom_TestCotton2"));
	EXPECT_NE(id, DefaultResourceId("other", "Custom_TestCotton"));
	// "a/b" by hand: FNV-1a 64 and FNV-1a 32 of the three bytes.
	EXPECT_EQ(DefaultResourceId("a", "b"), "e620c3190468cf61"
	                                       "3a8e75c1");
}

TEST(ObjectsPack, ParsesAllPacksAndSkipsTheBadOnes)
{
	const std::vector<PackSource> sources {
	    {.folder = "zeta", .text = R"({"format": 1, "id": "alpha", "resources": []})"},
	    {.folder = "broken", .text = R"({"format": 9})"},
	    {.folder = "beta", .text = R"({"format": 1, "id": "beta", "resources": []})"},
	    {.folder = "alpha", .text = R"({"format": 1, "id": "alpha", "resources": []})"},
	};
	const auto set = ParsePacks(sources);
	ASSERT_EQ(set.packs.size(), 2u);
	EXPECT_EQ(set.packs[0].id, "alpha");
	EXPECT_EQ(set.packs[0].folder, "alpha");
	EXPECT_EQ(set.packs[1].id, "beta");
	ASSERT_EQ(set.errors.size(), 2u);
	EXPECT_EQ(set.errors[0].pack, "broken");
	EXPECT_TRUE(set.errors[0].message.starts_with("format:"));
	EXPECT_EQ(set.errors[1].pack, "zeta");
	EXPECT_TRUE(set.errors[1].message.starts_with("id:"));
}

TEST(ObjectsPack, ComparesNamesWithoutCase)
{
	EXPECT_TRUE(EqualsIgnoreCase("_Prefabs/RawMaterials/PickupCotton", "_prefabs/rawmaterials/pickupcotton"));
	EXPECT_FALSE(EqualsIgnoreCase("Pickup", "Pickups"));
	EXPECT_FALSE(EqualsIgnoreCase("a_b", "a-b"));
}

TEST(ObjectsPack, ReadsTypedThrowables)
{
	constexpr std::string_view k_Text = R"({
  "format": 2, "id": "rocks",
  "throwables": [
    {"name": "MOAR_Rock", "template": "MA_Throwable_Boulder1", "mesh": "models/rock.moamesh", "collider": "mesh",
     "grip": [0, 1.5, 0], "title": "Rock", "mass": 300, "damage": 60.5, "maxImpacts": 3, "starterPack": true}
  ]})";
	auto pack = ParsePack(k_Text);
	ASSERT_TRUE(pack) << pack.GetError().message;
	EXPECT_EQ(pack->format, k_PackFormatTyped);
	ASSERT_EQ(pack->resources.size(), 1u);
	const auto& rock = pack->resources[0];
	EXPECT_EQ(rock.kind, PackKind::Throwable);
	EXPECT_EQ(rock.templateName, "MA_Throwable_Boulder1");
	EXPECT_EQ(rock.collider, ColliderMode::Mesh);
	EXPECT_EQ(rock.grip, (std::array {0.0f, 1.5f, 0.0f}));
	ASSERT_EQ(rock.values.size(), 5u);
	EXPECT_EQ(rock.values[0], (std::pair<std::string, KnackValue> {"title", std::string("Rock")}));
	EXPECT_EQ(rock.values[1], (std::pair<std::string, KnackValue> {"mass", 300.0}));
	EXPECT_EQ(rock.values[2], (std::pair<std::string, KnackValue> {"damage", 60.5}));
	EXPECT_EQ(rock.values[3], (std::pair<std::string, KnackValue> {"maxImpacts", 3.0}));
	EXPECT_EQ(rock.values[4], (std::pair<std::string, KnackValue> {"starterPack", true}));
	ASSERT_NE(FindPackField(PackKind::Throwable, "mass"), nullptr);
	EXPECT_EQ(FindPackField(PackKind::Throwable, "mass")->field, "m_throwableMass");
	EXPECT_EQ(FindPackField(PackKind::Resource, "mass"), nullptr);
}

TEST(ObjectsPack, RefusesBadTypedValues)
{
	const auto throwable = [](std::string_view extra) {
		return ErrorOf(std::format(R"({{"format": 2, "id": "p", "throwables": [{{"name": "T", "template": "B"{}}}]}})", extra));
	};
	EXPECT_EQ(throwable(""), "");
	EXPECT_TRUE(throwable(R"(, "mass": 0)").starts_with("throwables[0].mass:")) << throwable(R"(, "mass": 0)");
	EXPECT_TRUE(throwable(R"(, "mass": "5")").starts_with("throwables[0].mass:"));
	EXPECT_TRUE(throwable(R"(, "maxImpacts": 1.5)").starts_with("throwables[0].maxImpacts:"));
	EXPECT_TRUE(throwable(R"(, "starterPack": 1)").starts_with("throwables[0].starterPack:"));
	EXPECT_TRUE(throwable(R"(, "row": {})").starts_with("throwables[0].row:"));
	EXPECT_TRUE(throwable(R"(, "grip": [1, 2])").starts_with("throwables[0].grip:"));
	EXPECT_TRUE(throwable(R"(, "title": 5)").starts_with("throwables[0].title:"));
	// The typed lists need format 2, and a resource cannot have typed values.
	EXPECT_TRUE(ErrorOf(R"({"format": 1, "id": "p", "resources": [], "throwables": []})").starts_with("throwables:"));
	EXPECT_TRUE(ErrorOf(PackWithResource(R"(, "mass": 5)")).starts_with("resources[0].mass:"));
	// Format 2 needs no list of resources.
	EXPECT_EQ(ErrorOf(R"({"format": 2, "id": "p"})"), "");
	// A name is unique over all the lists.
	EXPECT_TRUE(ErrorOf(R"({"format": 2, "id": "p", "resources": [{"name": "A", "template": "Wood"}],)"
	                    R"( "throwables": [{"name": "a", "template": "B"}]})")
	                .starts_with("throwables[0].name:"));
}

TEST(ObjectsPack, ReadsWeaponsAndSpells)
{
	constexpr std::string_view k_Text = R"({
  "format": 2, "id": "kit",
  "weapons": [{"name": "MOAR_Blade", "template": "Weapon_Blade_Sword_A", "tip": [0, 3, 0], "attack": 0.6, "tags": "Blade"}],
  "spells": [{"power": "Fireball", "level": 6, "manaCost": 60, "icon": "ARC_fireball"}]})";
	auto pack = ParsePack(k_Text);
	ASSERT_TRUE(pack) << pack.GetError().message;
	ASSERT_EQ(pack->resources.size(), 2u);
	const auto& blade = pack->resources[0];
	EXPECT_EQ(blade.kind, PackKind::Weapon);
	EXPECT_EQ(blade.grip, (std::array {0.0f, 3.0f, 0.0f}));
	EXPECT_EQ(PackValue(blade, "attack"), KnackValue(0.6));
	const auto& spell = pack->resources[1];
	EXPECT_EQ(spell.kind, PackKind::Spell);
	EXPECT_EQ(spell.name, "Fireball6");
	EXPECT_EQ(spell.templateName, "Fireball");
	EXPECT_EQ(PackValue(spell, "level"), KnackValue(6.0));
	EXPECT_FALSE(HasModel(PackKind::Spell));

	const auto spellError = [](std::string_view item) {
		return ErrorOf(std::format(R"({{"format": 2, "id": "p", "spells": [{}]}})", item));
	};
	EXPECT_TRUE(spellError(R"({"power": "Icebolt", "level": 6})").starts_with("spells[0].power:"));
	EXPECT_TRUE(spellError(R"({"power": "Fireball"})").starts_with("spells[0].level:"));
	EXPECT_TRUE(spellError(R"({"power": "Fireball", "level": 1})").starts_with("spells[0].level:"));
	EXPECT_TRUE(spellError(R"({"power": "Fireball", "level": 6, "model": "a.glb"})").starts_with("spells[0].model:"));
	EXPECT_TRUE(ErrorOf(R"({"format": 2, "id": "p", "weapons": [{"name": "B", "template": "T", "grip": [0, 0, 0]}]})")
	                .starts_with("weapons[0].grip:"));

	// The writer gives the same pack back.
	auto again = ParsePack(WritePack(*pack));
	ASSERT_TRUE(again) << again.GetError().message;
	ASSERT_EQ(again->resources.size(), 2u);
	EXPECT_EQ(again->resources[0].grip, blade.grip);
	EXPECT_EQ(again->resources[1].name, "Fireball6");
	EXPECT_EQ(again->resources[1].values, spell.values);
}
