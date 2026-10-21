/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <string>
#include <string_view>
#include <vector>

#include <core/LoaderSettings.h>
#include <core/ModCatalogue.h>
#include <gtest/gtest.h>
#include <modzip/ModInfo.h>
#include <modzip/Zip.h>

namespace
{
using namespace moa_mod::mods;
namespace mz = moa::modzip;

constexpr auto k_Build = "8a91370f9f7845a695fb431c39155434";
constexpr auto k_OtherBuild = "2ec5298a2ec5298a2ec5298a2ec5298a";

std::vector<std::byte> Bytes(std::string_view text)
{
	const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
	return {bytes.begin(), bytes.end()};
}

std::string Text(const std::vector<std::byte>& bytes)
{
	return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/// A mod zip with a map, a pack, or both.
std::vector<std::byte> ModZip(std::string_view id, bool map, bool pack, std::string_view build = k_Build,
                              std::string_view version = "1")
{
	mz::ModInfo info {.id = std::string(id),
	                  .name = "Mod " + std::string(id),
	                  .version = std::string(version),
	                  .buildGuids = {std::string(build)}};
	std::vector<mz::ZipFile> files;
	if (map)
	{
		info.map = true;
		files.push_back({.name = "map/manifest.json", .bytes = Bytes("{\"map\": \"" + std::string(id) + "\"}")});
	}
	if (pack)
	{
		info.packs = {"crate"};
		files.push_back({.name = "packs/crate/pack.json", .bytes = Bytes("{}")});
		files.push_back({.name = "packs/crate/models/crate.moamesh", .bytes = Bytes("mesh")});
	}
	files.push_back({.name = "mod.json", .bytes = Bytes(mz::ModInfoJson(info))});
	return *mz::WriteZip(files);
}

std::vector<CatalogueMod> Open(std::initializer_list<std::pair<std::string_view, std::vector<std::byte>>> zips)
{
	std::vector<CatalogueMod> mods;
	for (const auto& [file, bytes] : zips)
	{
		mods.push_back(OpenModZip(std::string(file), bytes, k_Build));
	}
	return mods;
}

std::vector<std::string> Ids(const ModCatalogue& catalogue)
{
	std::vector<std::string> ids;
	for (const auto* mod : catalogue.Loaded())
	{
		ids.push_back(mod->info->id);
	}
	return ids;
}
} // namespace

TEST(ModCatalogue, OpensAGoodZip)
{
	const auto mod = OpenModZip("a.zip", ModZip("alpha", true, true), k_Build);
	ASSERT_EQ(mod.state, ModState::On) << mod.error;
	ASSERT_TRUE(mod.info);
	EXPECT_EQ(mod.info->id, "alpha");
	ASSERT_TRUE(mod.archive);
	const auto map = ZipFolderReader(mod.archive, "map/");
	EXPECT_EQ(Text(*map("manifest.json")), "{\"map\": \"alpha\"}");
	const auto pack = ZipFolderReader(mod.archive, "packs/crate/");
	EXPECT_EQ(Text(*pack("models/crate.moamesh")), "mesh");
	// The reader stays in its folder.
	EXPECT_FALSE(pack("../../mod.json"));
	EXPECT_FALSE(pack("/mod.json"));
	EXPECT_FALSE(pack("models/"));
	EXPECT_FALSE(map("other.json"));
}

TEST(ModCatalogue, RefusesBadZips)
{
	EXPECT_NE(OpenModZip("x.zip", Bytes("not a zip"), k_Build).error, "");
	EXPECT_NE(OpenModZip("x.zip", *mz::WriteZip(std::vector<mz::ZipFile> {{.name = "a.txt", .bytes = {}}}), k_Build)
	              .error.find("mod.json"),
	          std::string::npos);
	const auto other = OpenModZip("x.zip", ModZip("alpha", true, false, k_OtherBuild), k_Build);
	EXPECT_EQ(other.state, ModState::Refused);
	EXPECT_TRUE(other.info) << "the menu bar shows the name of a mod for another build";
	EXPECT_NE(other.error.find("game build"), std::string::npos);
	const auto big = OpenModZip("x.zip", ModZip("alpha", true, true), k_Build, mz::ZipLimits {.maxEntryBytes = 3});
	EXPECT_EQ(big.state, ModState::Refused);
	EXPECT_FALSE(big.archive);
}

TEST(ModCatalogue, LoadsInTheOrderOfTheIdsAndRefusesTheSameId)
{
	auto catalogue = BuildCatalogue(Open({{"z.zip", ModZip("beta", false, true)},
	                                      {"b.zip", ModZip("alpha", false, true)},
	                                      {"broken.zip", Bytes("x")},
	                                      {"c.zip", ModZip("gamma", false, true)},
	                                      {"d.zip", ModZip("gamma", true, false)}}),
	                                moa_mod::loader::LoaderSettings {});
	EXPECT_EQ(Ids(catalogue), (std::vector<std::string> {"alpha", "beta"}));
	ASSERT_EQ(catalogue.mods.size(), 5u);
	EXPECT_EQ(catalogue.mods[2].file, "c.zip");
	EXPECT_EQ(catalogue.mods[2].state, ModState::Refused);
	EXPECT_NE(catalogue.mods[2].error.find("same id"), std::string::npos);
	EXPECT_EQ(catalogue.mods[3].state, ModState::Refused);
	EXPECT_EQ(catalogue.mods[4].file, "broken.zip");
	EXPECT_EQ(catalogue.map.kind, MapChoice::Kind::None) << "the only mod with a map has the id of another mod";
}

TEST(ModCatalogue, TheNewestVersionOfAnIdLoads)
{
	auto catalogue = BuildCatalogue(Open({{"old.zip", ModZip("alpha", false, true, k_Build, "1.9")},
	                                      {"new.zip", ModZip("alpha", true, false, k_Build, "1.10")},
	                                      {"older.zip", ModZip("alpha", false, true, k_Build, "1.2.0")}}),
	                                moa_mod::loader::LoaderSettings {});
	ASSERT_EQ(Ids(catalogue), (std::vector<std::string> {"alpha"}));
	EXPECT_EQ(catalogue.FindLoaded("alpha")->file, "new.zip");
	for (const auto& mod : catalogue.mods)
	{
		if (mod.file != "new.zip")
		{
			EXPECT_EQ(mod.state, ModState::Refused) << mod.file;
			EXPECT_FALSE(mod.archive) << mod.file;
			EXPECT_NE(mod.error.find("newer version 1.10"), std::string::npos) << mod.error;
		}
	}
	EXPECT_EQ(catalogue.map.kind, MapChoice::Kind::Mod) << "the newest zip gives the map";
}

TEST(ModCatalogue, AZipThatFailsItsChecksDoesNotHideAnOlderVersion)
{
	auto catalogue = BuildCatalogue(Open({{"old.zip", ModZip("alpha", false, true, k_Build, "1")},
	                                      {"other-build.zip", ModZip("alpha", false, true, k_OtherBuild, "2")}}),
	                                moa_mod::loader::LoaderSettings {});
	ASSERT_EQ(Ids(catalogue), (std::vector<std::string> {"alpha"}));
	EXPECT_EQ(catalogue.FindLoaded("alpha")->file, "old.zip");
}

TEST(ModCatalogue, AppliesTheSwitchesAndChoosesTheMap)
{
	const auto zips = [] {
		return Open({{"a.zip", ModZip("alpha", true, false)},
		             {"b.zip", ModZip("beta", true, true)},
		             {"c.zip", ModZip("gamma", false, true)}});
	};
	moa_mod::loader::LoaderSettings settings;
	auto catalogue = BuildCatalogue(zips(), settings);
	EXPECT_EQ(catalogue.map, (MapChoice {.kind = MapChoice::Kind::Mod, .name = "alpha"})) << "the first mod with a map";
	EXPECT_TRUE(catalogue.mods[0].mapActive);
	EXPECT_FALSE(catalogue.mods[1].mapActive);

	settings.mods.disabled = {"alpha"};
	catalogue = BuildCatalogue(zips(), settings);
	EXPECT_EQ(catalogue.mods[0].state, ModState::Off);
	EXPECT_EQ(Ids(catalogue), (std::vector<std::string> {"beta", "gamma"}));
	EXPECT_EQ(catalogue.map.name, "beta");

	settings.mods.disabled.clear();
	settings.mods.map = "beta";
	EXPECT_EQ(BuildCatalogue(zips(), settings).map.name, "beta");
	settings.mods.map = "";
	EXPECT_EQ(BuildCatalogue(zips(), settings).map.kind, MapChoice::Kind::None);
	settings.mods.map = "gamma";
	catalogue = BuildCatalogue(zips(), settings);
	EXPECT_EQ(catalogue.map.name, "alpha") << "gamma has no map";
	EXPECT_NE(catalogue.mapNote, "");
	settings.maps.active = "dev-map";
	EXPECT_EQ(BuildCatalogue(zips(), settings).map, (MapChoice {.kind = MapChoice::Kind::Folder, .name = "dev-map"}));
}

TEST(ModCatalogue, EditsTheSettings)
{
	moa_mod::loader::LoaderSettings settings;
	auto edit = EditModEnabled(settings, "beta", false);
	ASSERT_TRUE(edit) << edit.GetError().message;
	EXPECT_EQ(settings.mods.disabled, (std::vector<std::string> {"beta"}));
	EXPECT_EQ(edit->patch, nlohmann::json::parse(R"({"mods": {"disabled": ["beta"]}})"));
	ASSERT_TRUE(EditModEnabled(settings, "alpha", false));
	EXPECT_EQ(settings.mods.disabled, (std::vector<std::string> {"alpha", "beta"}));
	ASSERT_TRUE(EditModEnabled(settings, "beta", true));
	EXPECT_EQ(settings.mods.disabled, (std::vector<std::string> {"alpha"}));
	EXPECT_FALSE(EditModEnabled(settings, "../x", false));

	edit = EditMapChoice(settings, MapChoice {.kind = MapChoice::Kind::Folder, .name = "dev"});
	ASSERT_TRUE(edit);
	EXPECT_EQ(settings.maps.active, "dev");
	EXPECT_FALSE(settings.mods.map);
	EXPECT_EQ(edit->patch, nlohmann::json::parse(R"({"maps": {"active": "dev"}, "mods": {"map": null}})"));
	ASSERT_TRUE(EditMapChoice(settings, MapChoice {.kind = MapChoice::Kind::Mod, .name = "beta"}));
	EXPECT_EQ(settings.maps.active, "");
	EXPECT_EQ(settings.mods.map, "beta");
	ASSERT_TRUE(EditMapChoice(settings, MapChoice {}));
	EXPECT_EQ(settings.mods.map, "");
	edit = EditMapChoice(settings, MapChoice {.kind = MapChoice::Kind::Automatic});
	ASSERT_TRUE(edit);
	EXPECT_FALSE(settings.mods.map);
	EXPECT_EQ(edit->patch, nlohmann::json::parse(R"({"maps": {"active": ""}, "mods": {"map": null}})"));
	EXPECT_FALSE(EditMapChoice(settings, MapChoice {.kind = MapChoice::Kind::Folder, .name = "../x"}));
	EXPECT_FALSE(EditMapChoice(settings, MapChoice {.kind = MapChoice::Kind::Mod, .name = "Bad Id"}));
}

TEST(ModCatalogue, ReadsAndWritesTheModsSettings)
{
	auto settings = moa_mod::loader::ParseLoaderSettings(R"({"mods": {"disabled": ["a", "b-c"], "map": "a"}})");
	ASSERT_TRUE(settings) << settings.GetError().message;
	EXPECT_EQ(settings->mods.disabled, (std::vector<std::string> {"a", "b-c"}));
	EXPECT_EQ(settings->mods.map, "a");
	auto again = moa_mod::loader::ParseLoaderSettings(moa_mod::loader::LoaderSettingsJson(*settings).dump());
	ASSERT_TRUE(again);
	EXPECT_EQ(*again, *settings);
	EXPECT_FALSE(moa_mod::loader::ParseLoaderSettings(R"({"mods": {"disabled": "a"}})"));
	EXPECT_FALSE(moa_mod::loader::ParseLoaderSettings(R"({"mods": {"disabled": ["../a"]}})"));
	EXPECT_FALSE(moa_mod::loader::ParseLoaderSettings(R"({"mods": {"map": 3}})"));
	EXPECT_FALSE(moa_mod::loader::ParseLoaderSettings(R"({"mods": {"other": true}})"));
	EXPECT_TRUE(moa_mod::loader::ParseLoaderSettings(R"({"mods": {"map": ""}})"));
	EXPECT_FALSE(moa_mod::loader::ParseLoaderSettings("{}")->mods.map) << "no value: the first mod with a map";

	moa_mod::loader::LoaderSettings next = *settings;
	next.mods.disabled.clear();
	const auto change = moa_mod::loader::CompareLoaderSettings(*settings, next);
	EXPECT_EQ(change.restartOnly, std::vector<std::string> {"mods"});
	EXPECT_EQ(moa_mod::loader::LiveLoaderSettings(*settings, next).mods, settings->mods);
}
