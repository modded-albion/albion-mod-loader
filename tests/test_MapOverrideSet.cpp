/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <map>
#include <string>
#include <vector>

#include <core/LoaderSettings.h>
#include <core/MapOverrideSet.h>
#include <core/ModCatalogue.h>
#include <gtest/gtest.h>
#include <mapoverride/MapOverride.h>
#include <mod_core/SettingsFields.h>

namespace
{
using namespace moa_mod::maps;
namespace mo = moa::mapoverride;
using Decision = MapOverrideSet::Decision;

constexpr auto k_Build = "8a91370f9f7845a695fb431c39155434";

std::vector<std::byte> Bytes(std::string_view text)
{
	const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
	return {bytes.begin(), bytes.end()};
}

/// The files of a test override, as the editor writes them, and a reader of them.
struct FakeFolder
{
	std::map<std::string, std::vector<std::byte>> files;

	explicit FakeFolder(const mo::MapOverrideContent& content)
	{
		auto built = mo::BuildMapOverride(content);
		EXPECT_TRUE(built) << built.GetError().message;
		for (auto& file : *built)
		{
			files.emplace(file.path, std::move(file.bytes));
		}
	}

	[[nodiscard]] OverrideFileReader Reader() const
	{
		return [this](const std::string& relative) -> moa::Result<std::vector<std::byte>> {
			const auto found = files.find(relative);
			if (found == files.end())
			{
				return moa::MakeError("no file {}", relative);
			}
			return found->second;
		};
	}
};

mo::MapOverrideContent TestContent()
{
	mo::MapOverrideContent content {.buildGuid = k_Build, .tool = "test", .made = "now"};
	content.texts.push_back({.name = "seedmoa", .paths = {"seedmoa"}, .original = "old seed", .text = "new seed"});
	content.texts.push_back(
	    {.name = "Chapter1Flow", .paths = {"flows/chapter1v2/chapter1flow"}, .original = "old flow", .text = "new flow"});
	content.coastMap =
	    mo::BytesChange {.name = "coastmap", .paths = {"coastmap"}, .original = Bytes("1234"), .changed = Bytes("1299")};
	content.heights = mo::HeightsChange {
	    .resolution = 5, .rect = {.column = 1, .row = 1, .width = 2, .height = 1}, .before = {100, 200}, .after = {150, 250}};
	content.holders = {{.name = "TreeHolder1", .before = 1.5f, .after = 2.5f}};
	return content;
}
} // namespace

TEST(MapOverrideSet, LoadsACorrectOverride)
{
	const FakeFolder folder(TestContent());
	auto set = MapOverrideSet::Load("test", k_Build, folder.Reader());
	ASSERT_TRUE(set) << set.GetError().message;
	EXPECT_EQ(set->Items().size(), 5u);
	EXPECT_EQ(set->FindPath("SeedMoa"), std::optional<size_t>(0));
	EXPECT_EQ(set->FindPath("Flows/Chapter1V2/Chapter1Flow"), std::optional<size_t>(1));
	EXPECT_FALSE(set->FindPath("flows/other"));
	EXPECT_TRUE(set->FindKind(mo::ItemKind::Heights));
	EXPECT_FALSE(set->FindKind(mo::ItemKind::TerrainTrees));
	const auto& heights = set->Items()[*set->FindKind(mo::ItemKind::Heights)];
	EXPECT_EQ(heights.heights, (std::vector<int16_t> {150, 250}));
}

TEST(MapOverrideSet, RefusesAnotherBuildAndLoadsNothing)
{
	const FakeFolder folder(TestContent());
	auto set = MapOverrideSet::Load("test", "2ec5298ae9c544e7a914b0913b1119cc", folder.Reader());
	ASSERT_FALSE(set);
	EXPECT_NE(set.GetError().message.find("build"), std::string::npos);
}

TEST(MapOverrideSet, RefusesAChangedOrMissingFile)
{
	FakeFolder changed(TestContent());
	for (auto& [path, bytes] : changed.files)
	{
		if (path.starts_with("text/"))
		{
			bytes.push_back(std::byte {'!'});
			break;
		}
	}
	EXPECT_FALSE(MapOverrideSet::Load("test", k_Build, changed.Reader()));
	FakeFolder missing(TestContent());
	missing.files.erase("land/heights.bin");
	EXPECT_FALSE(MapOverrideSet::Load("test", k_Build, missing.Reader()));
	FakeFolder noManifest(TestContent());
	noManifest.files.erase(std::string(mo::k_ManifestFile));
	EXPECT_FALSE(MapOverrideSet::Load("test", k_Build, noManifest.Reader()));
	EXPECT_FALSE(MapOverrideSet::Load("../test", k_Build, changed.Reader()));
}

TEST(MapOverrideSet, AppliesAnItemOnlyOnTheOriginalData)
{
	const FakeFolder folder(TestContent());
	auto set = MapOverrideSet::Load("test", k_Build, folder.Reader());
	ASSERT_TRUE(set);
	EXPECT_EQ(set->Decide(0, Bytes("old seed")), Decision::Apply);
	set->Served(0, "served");
	EXPECT_EQ(set->Items()[0].state, ItemState::Applied);
	const auto coast = *set->FindPath("coastmap");
	EXPECT_EQ(set->Decide(coast, Bytes("1234")), Decision::Apply);
	// A second load of the changed asset finds the data of the override.
	EXPECT_EQ(set->Decide(coast, Bytes("1299")), Decision::AlreadyApplied);
	const auto heights = *set->FindKind(mo::ItemKind::Heights);
	const std::vector<int16_t> before {100, 200};
	const std::vector<int16_t> after {150, 250};
	EXPECT_EQ(set->Decide(heights, mo::HeightBytes(before)), Decision::Apply);
	EXPECT_EQ(set->Decide(heights, mo::HeightBytes(after)), Decision::AlreadyApplied);
	const auto holders = *set->FindKind(mo::ItemKind::TreeHolders);
	const std::vector<mo::HolderMove> live {{.name = "TreeHolder1", .before = 1.5f, .after = 2.5f}};
	EXPECT_EQ(set->Decide(holders, mo::HolderOriginalBytes(live)), Decision::Apply);
	const std::vector<mo::HolderMove> moved {{.name = "TreeHolder1", .before = 2.5f, .after = 2.5f}};
	EXPECT_EQ(set->Decide(holders, mo::HolderOriginalBytes(moved)), Decision::AlreadyApplied);
	EXPECT_FALSE(set->LandStopped());
}

TEST(MapOverrideSet, ARefusedTextStopsOnlyThatText)
{
	const FakeFolder folder(TestContent());
	auto set = MapOverrideSet::Load("test", k_Build, folder.Reader());
	ASSERT_TRUE(set);
	EXPECT_EQ(set->Decide(1, Bytes("a flow of another build")), Decision::Refuse);
	EXPECT_EQ(set->Items()[1].state, ItemState::Refused);
	EXPECT_FALSE(set->Items()[1].message.empty());
	EXPECT_TRUE(set->GroupStopped(1));
	// The flow stays refused at the next load, also with the correct data.
	EXPECT_EQ(set->Decide(1, Bytes("old flow")), Decision::Refuse);
	// The other text and the land do not change.
	EXPECT_FALSE(set->GroupStopped(0));
	EXPECT_FALSE(set->LandStopped());
	EXPECT_EQ(set->Decide(0, Bytes("old seed")), Decision::Apply);
	const auto coast = *set->FindPath("coastmap");
	EXPECT_EQ(set->Decide(coast, Bytes("1234")), Decision::Apply);
}

TEST(MapOverrideSet, ARefusedLandItemStopsTheWholeLandGroup)
{
	const FakeFolder folder(TestContent());
	auto set = MapOverrideSet::Load("test", k_Build, folder.Reader());
	ASSERT_TRUE(set);
	const auto heights = *set->FindKind(mo::ItemKind::Heights);
	const auto coast = *set->FindPath("coastmap");
	const std::vector<int16_t> other {1, 2};
	std::string reason;
	// A check changes nothing.
	EXPECT_EQ(set->Check(heights, mo::HeightBytes(other), &reason), Decision::Refuse);
	EXPECT_FALSE(reason.empty());
	EXPECT_FALSE(set->LandStopped());
	EXPECT_EQ(set->Decide(heights, mo::HeightBytes(other)), Decision::Refuse);
	EXPECT_TRUE(set->LandStopped());
	EXPECT_EQ(set->Items()[coast].state, ItemState::Disabled);
	EXPECT_EQ(set->Decide(coast, Bytes("1234")), Decision::Refuse);
	EXPECT_EQ(set->Decide(0, Bytes("old seed")), Decision::Apply);
	const auto described = set->Describe();
	EXPECT_TRUE(described.at("landStopped").get<bool>());
	EXPECT_EQ(described.at("items").at(heights).at("state"), "refused");
}

TEST(MapOverrideSet, AFailedLandChangeStopsTheLandGroup)
{
	const FakeFolder folder(TestContent());
	auto set = MapOverrideSet::Load("test", k_Build, folder.Reader());
	ASSERT_TRUE(set);
	set->Fail(*set->FindKind(mo::ItemKind::TreeHolders), "the memory is not writable");
	EXPECT_TRUE(set->LandStopped());
	EXPECT_EQ(set->Items()[*set->FindKind(mo::ItemKind::Heights)].state, ItemState::Disabled);
	EXPECT_FALSE(set->GroupStopped(0));
}

TEST(MapOverrideSettings, ReadsTheActiveOverride)
{
	auto settings = moa_mod::loader::ParseLoaderSettings(R"({"maps": {"active": "test-1"}})");
	ASSERT_TRUE(settings) << settings.GetError().message;
	EXPECT_EQ(settings->maps.active, "test-1");
	EXPECT_TRUE(moa_mod::loader::ParseLoaderSettings(R"({"maps": {"active": ""}})"));
	EXPECT_FALSE(moa_mod::loader::ParseLoaderSettings(R"({"maps": {"active": "../x"}})"));
	EXPECT_FALSE(moa_mod::loader::ParseLoaderSettings(R"({"maps": {"active": 3}})"));
	const auto json = moa_mod::loader::LoaderSettingsJson(*settings);
	EXPECT_EQ(json.at("maps").at("active"), "test-1");
}

TEST(MapOverrideSettings, TheModListChangesTheOverrideAfterARestart)
{
	// The Mod list of the menu bar chooses the map, not a field of the Mods menu.
	EXPECT_EQ(moa_mod::settings::FindField<moa_mod::loader::LoaderSettings>(moa_mod::loader::LoaderFields(), "maps", "active"),
	          nullptr);
	moa_mod::loader::LoaderSettings settings;
	auto edit = moa_mod::mods::EditMapChoice(
	    settings, moa_mod::mods::MapChoice {.kind = moa_mod::mods::MapChoice::Kind::Folder, .name = "edited"});
	ASSERT_TRUE(edit) << edit.GetError().message;
	EXPECT_EQ(settings.maps.active, "edited");
	EXPECT_EQ(edit->patch.at("maps").at("active"), "edited");
	EXPECT_FALSE(moa_mod::mods::EditMapChoice(
	    settings, moa_mod::mods::MapChoice {.kind = moa_mod::mods::MapChoice::Kind::Folder, .name = "a/b"}));
	EXPECT_EQ(settings.maps.active, "edited");
	const auto change = moa_mod::loader::CompareLoaderSettings(moa_mod::loader::LoaderSettings {}, settings);
	EXPECT_EQ(change.restartOnly, std::vector<std::string> {"maps.active"});
	EXPECT_EQ(moa_mod::loader::LiveLoaderSettings(moa_mod::loader::LoaderSettings {}, settings).maps.active, "");
}
