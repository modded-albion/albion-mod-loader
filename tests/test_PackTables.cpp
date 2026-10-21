/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <moa_mod/objects/PackTables.h>

using namespace moa_mod::objects;

namespace
{

constexpr std::string_view k_Decorations =
    R"([{"id":"aaaaaaaaaaaaaaaaaaaaaaaa","m_name":"MA_Throwable_Boulder1","m_prefabName":"MA_Throwable_Boulder1","m_location":"_Art/Decorations/Throwable","m_throwableMass":"200.00"}])";
constexpr std::string_view k_Powers =
    R"([{"id":"p5","m_indexer":"Dig5","m_name":"Dig","m_level":5,"m_manaCost":5},{"id":"p4","m_indexer":"Dig4","m_name":"Dig","m_level":4,"m_manaCost":4}])";
constexpr std::string_view k_Research =
    R"j([{"id":"r4","m_key":"Mystic::HandPowerDigL4","m_name":"HandPowerDigL4","m_linkToString":"Mystic::HandPowerDigL5","m_unlock":"m_handPowerDigLevel=4","m_position":"(0.00, 0.00, 0.00)"},)j"
    R"j({"id":"r5","m_key":"Mystic::HandPowerDigL5","m_name":"HandPowerDigL5","m_linkToString":"","m_unlock":"m_handPowerDigLevel=5","m_position":"(10.00, 0.00, 0.00)"}])j";

std::vector<Pack> Packs()
{
	PackResource rock;
	rock.kind = PackKind::Throwable;
	rock.name = rock.prefab = "MOAR_Rock";
	rock.templateName = "MA_Throwable_Boulder1";
	PackResource dig;
	dig.kind = PackKind::Spell;
	dig.name = dig.prefab = "Dig6";
	dig.templateName = "Dig";
	dig.values = {{"level", 6.0}};
	return {Pack {.folder = "f", .id = "kit", .name = "kit", .version = "1", .description = {}, .resources = {rock, dig}}};
}

} // namespace

TEST(PackTables, FindsTheTableOfAPath)
{
	EXPECT_EQ(PackTableOfPath("reactxls/ngbalance/ngknack/ngblockinfo"), PackTable::Block);
	EXPECT_EQ(PackTableOfPath("reactxls/ngbalance/ngknack/maresearchinfo"), PackTable::Research);
	EXPECT_FALSE(PackTableOfPath("reactxls/ngbalance/ngknack/ngproductinfo"));
	EXPECT_EQ(PackTablePath(PackTable::HandPower), "reactxls/ngbalance/ngknack/mahandpowerinfo");
	const auto packs = Packs();
	EXPECT_TRUE(PacksChangeTable(packs, PackTable::Decoration));
	EXPECT_TRUE(PacksChangeTable(packs, PackTable::Research));
	EXPECT_FALSE(PacksChangeTable(packs, PackTable::Block));
	EXPECT_FALSE(PacksChangeTable(packs, PackTable::Carriable));
}

TEST(PackTables, MergesEachTableAndAsksForTheOtherSpellTable)
{
	PackTableMerger merger(Packs());
	int loads = 0;
	const auto load = [&](PackTable table) -> std::optional<std::string> {
		++loads;
		EXPECT_EQ(table, PackTable::Research);
		return std::string(k_Research);
	};
	EXPECT_FALSE(merger.Merge(PackTable::Block, "[]", load));
	auto decorations = merger.Merge(PackTable::Decoration, k_Decorations, load);
	ASSERT_TRUE(decorations);
	EXPECT_NE(decorations->find("MOAR_Rock"), std::string::npos);
	ASSERT_EQ(merger.Merged().size(), 1u);
	EXPECT_EQ(merger.Merged()[0].prefabPath, "_Art/Decorations/MOAR/MOAR_Rock");
	ASSERT_EQ(merger.MergedIn(PackTable::Decoration).size(), 1u);
	EXPECT_TRUE(merger.MergedIn(PackTable::Block).empty());
	EXPECT_TRUE(merger.MergedIn(PackTable::HandPower).empty());

	auto powers = merger.Merge(PackTable::HandPower, k_Powers, load);
	ASSERT_TRUE(powers);
	EXPECT_EQ(loads, 1);
	EXPECT_NE(powers->find("\"m_indexer\":\"Dig6\""), std::string::npos);
	ASSERT_EQ(merger.Spells().size(), 1u);
	EXPECT_EQ(merger.Spells()[0].unlock, "m_handPowerDigLevel=6");
	// The research table comes from the same merge; the loader is not asked again.
	auto research = merger.Merge(PackTable::Research, k_Research, load);
	ASSERT_TRUE(research);
	EXPECT_EQ(loads, 1);
	EXPECT_NE(research->find("Mystic::HandPowerDigL6"), std::string::npos);
	EXPECT_TRUE(merger.TakeErrors().empty());

	// A table that is not JSON keeps the game text and gives an error.
	EXPECT_FALSE(merger.Merge(PackTable::Decoration, "not json", load));
	const auto errors = merger.TakeErrors();
	ASSERT_EQ(errors.size(), 1u);
	EXPECT_EQ(errors[0].pack, "reactxls/ngbalance/ngknack/ngdecorationinfo");
}
