/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <string>

#include <gtest/gtest.h>
#include <moa_mod/objects/Pack.h>
#include <moa_mod/objects/PackWriter.h>

using namespace moa_mod::objects;

namespace
{

constexpr std::string_view k_ModelPack = R"({
  "format": 1, "id": "broken_coffin", "name": "Broken Coffin", "version": "1.0.0",
  "description": "A coffin.",
  "resources": [
    {"name": "BrokenCoffin", "template": "Wood", "model": "models/BrokenCoffin.glb",
     "mesh": "models\\BrokenCoffin.moamesh",
     "materials": [{"name": "Coffin", "base": "models/base.png", "normal": "models/normal.png", "mask": "models/mask.png"},
                   {"base": "models/other.png"}],
     "scale": 1.0, "pivot": "bottom-centre", "collider": "box",
     "row": {"m_title": "Broken Coffin"},
     "credits": {"title": "Broken Coffin", "author": "An Author", "source": "https://example.com/coffin",
                 "licence": "CC-BY-4.0"}}
  ]})";

std::string ErrorOf(std::string_view text)
{
	auto pack = ParsePack(text);
	return pack ? std::string {} : pack.GetError().message;
}

PackResource MakeResource(std::string name)
{
	PackResource resource;
	resource.name = name;
	resource.templateName = "Wood";
	resource.prefab = std::move(name);
	return resource;
}

} // namespace

TEST(ObjectsPackWriter, ReadsTheModelFields)
{
	auto pack = ParsePack(k_ModelPack);
	ASSERT_TRUE(pack) << pack.GetError().message;
	EXPECT_EQ(pack->description, "A coffin.");
	const auto& coffin = pack->resources.at(0);
	EXPECT_EQ(coffin.mesh, "models/BrokenCoffin.moamesh");
	ASSERT_EQ(coffin.materials.size(), 2u);
	EXPECT_EQ(coffin.materials[0].name, "Coffin");
	EXPECT_EQ(coffin.materials[0].base, "models/base.png");
	EXPECT_EQ(coffin.materials[0].normal, "models/normal.png");
	EXPECT_EQ(coffin.materials[0].mask, "models/mask.png");
	EXPECT_EQ(coffin.materials[1].base, "models/other.png");
	EXPECT_FALSE(coffin.materials[1].normal);
	EXPECT_EQ(coffin.credits.author, "An Author");
	EXPECT_EQ(coffin.credits.licence, "CC-BY-4.0");
	EXPECT_EQ(coffin.collider, ColliderMode::Box);
}

TEST(ObjectsPackWriter, RefusesBadModelFields)
{
	const auto with = [](std::string_view extra) {
		return std::string(R"({"format": 1, "id": "p", "resources": [{"name": "Thing", "template": "Wood", )") +
		       std::string(extra) + "}]}";
	};
	EXPECT_NE(ErrorOf(with(R"("mesh": "../outside.moamesh")")).find("resources[0].mesh"), std::string::npos);
	EXPECT_NE(ErrorOf(with(R"("mesh": "C:/absolute.moamesh")")).find("resources[0].mesh"), std::string::npos);
	EXPECT_NE(ErrorOf(with(R"("materials": {})")).find("resources[0].materials"), std::string::npos);
	EXPECT_NE(ErrorOf(with(R"("materials": [{"base": "/x.png"}])")).find("resources[0].materials[0].base"), std::string::npos);
	EXPECT_NE(ErrorOf(with(R"("materials": [{"emission": "x.png"}])")).find("materials[0].emission"), std::string::npos);
	EXPECT_NE(ErrorOf(with(R"("credits": {"year": "2026"})")).find("credits.year"), std::string::npos);
	EXPECT_NE(ErrorOf(with(R"("credits": {"author": 5})")).find("credits.author"), std::string::npos);
	EXPECT_NE(ErrorOf(R"({"format": 1, "id": "p", "description": 3, "resources": []})").find("description"), std::string::npos);
}

TEST(ObjectsPackWriter, WriteThenParseGivesTheSamePack)
{
	auto pack = ParsePack(k_ModelPack);
	ASSERT_TRUE(pack) << pack.GetError().message;
	auto extra = MakeResource("Plain");
	extra.prefab = "PlainPrefab";
	extra.id = "0123456789abcdef01234567";
	extra.scale = 0.5f;
	extra.pivot = PivotMode::Explicit;
	extra.pivotPoint = {1.0f, 2.0f, 3.0f};
	extra.collider = ColliderMode::Mesh;
	pack->resources.push_back(extra);

	const auto text = WritePack(*pack);
	auto again = ParsePack(text);
	ASSERT_TRUE(again) << again.GetError().message << "\n" << text;
	EXPECT_EQ(again->id, pack->id);
	EXPECT_EQ(again->name, pack->name);
	EXPECT_EQ(again->version, pack->version);
	EXPECT_EQ(again->description, pack->description);
	ASSERT_EQ(again->resources.size(), 2u);
	for (size_t i = 0; i < 2; ++i)
	{
		const auto& a = pack->resources[i];
		const auto& b = again->resources[i];
		EXPECT_EQ(a.name, b.name);
		EXPECT_EQ(a.templateName, b.templateName);
		EXPECT_EQ(a.prefab, b.prefab);
		EXPECT_EQ(a.id, b.id);
		EXPECT_EQ(a.model, b.model);
		EXPECT_EQ(a.mesh, b.mesh);
		EXPECT_EQ(a.materials, b.materials);
		EXPECT_FLOAT_EQ(a.scale, b.scale);
		EXPECT_EQ(a.pivot, b.pivot);
		EXPECT_EQ(a.pivotPoint, b.pivotPoint);
		EXPECT_EQ(a.collider, b.collider);
		EXPECT_EQ(a.row, b.row);
		EXPECT_EQ(a.credits, b.credits);
	}
	// The text ends with a new line and keeps the keys in a fixed order.
	EXPECT_TRUE(text.ends_with("}\n"));
	EXPECT_LT(text.find("\"format\""), text.find("\"resources\""));
}

TEST(ObjectsPackWriter, PutResourceReplacesByNameOrAdds)
{
	Pack pack;
	pack.id = "p";
	EXPECT_FALSE(PutResource(pack, MakeResource("First")));
	EXPECT_FALSE(PutResource(pack, MakeResource("Second")));
	auto changed = MakeResource("FIRST");
	changed.scale = 2.0f;
	EXPECT_TRUE(PutResource(pack, changed));
	ASSERT_EQ(pack.resources.size(), 2u);
	EXPECT_EQ(pack.resources[0].name, "FIRST");
	EXPECT_FLOAT_EQ(pack.resources[0].scale, 2.0f);
	EXPECT_EQ(pack.resources[1].name, "Second");
}

TEST(ObjectsPackWriter, WritesTypedItemsInFormat2)
{
	Pack pack;
	pack.id = "mixed";
	pack.name = "mixed";
	auto rock = MakeResource("MOAR_Rock");
	rock.kind = PackKind::Throwable;
	rock.templateName = "MA_Throwable_Boulder1";
	rock.grip = std::array {0.0f, 2.0f, 0.25f};
	rock.values = {{"title", std::string("Rock")}, {"mass", 250.0}, {"maxImpacts", 2.0}, {"onDecorationStrip", false}};
	EXPECT_FALSE(PutResource(pack, rock));
	EXPECT_FALSE(PutResource(pack, MakeResource("Log")));
	// The carried resource goes before the throwable, as in the file.
	ASSERT_EQ(pack.resources.size(), 2u);
	EXPECT_EQ(pack.resources[0].name, "Log");

	const auto text = WritePack(pack);
	EXPECT_NE(text.find("\"format\": 2"), std::string::npos) << text;
	EXPECT_NE(text.find("\"maxImpacts\": 2,"), std::string::npos) << text;
	auto again = ParsePack(text);
	ASSERT_TRUE(again) << again.GetError().message << "\n" << text;
	ASSERT_EQ(again->resources.size(), 2u);
	EXPECT_EQ(again->resources[1].kind, PackKind::Throwable);
	EXPECT_EQ(again->resources[1].grip, rock.grip);
	EXPECT_EQ(again->resources[1].values, rock.values);

	// A pack with only carried resources keeps format 1.
	Pack plain;
	plain.id = "plain";
	plain.name = "plain";
	EXPECT_NE(WritePack(plain).find("\"format\": 1"), std::string::npos);
	EXPECT_NE(WritePack(plain).find("\"resources\": []"), std::string::npos);
}
