/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cstdint>

#include <array>

#include <gtest/gtest.h>
#include <moa_mod/loader/LoaderSettings.h>
#include <moa_mod/objects/ResourceLoads.h>

using namespace moa_mod;
using namespace moa_mod::objects;

TEST(ObjectsResourceLoads, FindsTheEntryOfAMethodByItsCodeAndMethod)
{
	// A class header, then entries of two words: code, method.
	const std::array<uintptr_t, 10> words {0x1111, 0x2222, 0xA0, 0xB0, 0xA1, 0xB1, 0xA2, 0xB2, 0x0, 0x0};
	EXPECT_EQ(FindMethodEntries(words, 0xA1, 0xB1), (std::vector<size_t> {4}));
	EXPECT_TRUE(FindMethodEntries(words, 0xA1, 0xB2).empty());
	EXPECT_TRUE(FindMethodEntries(words, 0xA1, 0).empty());
	// Code 0 accepts any code that is not 0.
	EXPECT_EQ(FindMethodEntries(words, 0, 0xB1), (std::vector<size_t> {4}));
	EXPECT_TRUE(FindMethodEntries(words, 0, 0x0).empty());
	// The pair at the end of the block, and a pair that the block cuts.
	EXPECT_EQ(FindMethodEntries(std::span(words).first(8), 0xA2, 0xB2), (std::vector<size_t> {6}));
	EXPECT_TRUE(FindMethodEntries(std::span(words).first(7), 0xA2, 0xB2).empty());
}

TEST(ObjectsResourceLoads, FindsEachEntryWhenTheMethodIsInTheTableTwoTimes)
{
	const std::array<uintptr_t, 6> words {0xA0, 0xB0, 0x5, 0xA0, 0xB0, 0x6};
	EXPECT_EQ(FindMethodEntries(words, 0xA0, 0xB0), (std::vector<size_t> {0, 3}));
}

TEST(ObjectsResourceLoads, NormalPathIsLowerCaseWithForwardSlashes)
{
	EXPECT_EQ(NormalResourcePath("_Prefabs\\RawMaterials/PickupCotton"), "_prefabs/rawmaterials/pickupcotton");
	EXPECT_EQ(NormalResourcePath(""), "");
}

TEST(ObjectsResourceLoads, TraceCountsEachPathAndTypeAndKeepsTheOrder)
{
	LoadTrace trace;
	EXPECT_TRUE(trace.Add("REACTXLS/NGBalance/NGKnack/NGCarriableResources", "UnityEngine.TextAsset"));
	EXPECT_TRUE(trace.Add("_Prefabs/RawMaterials/PickupCotton", "UnityEngine.GameObject"));
	EXPECT_FALSE(trace.Add("_Prefabs/RawMaterials/PickupCotton", "UnityEngine.GameObject"));
	EXPECT_TRUE(trace.Add("_Prefabs/RawMaterials/PickupCotton", "UnityEngine.Object"));
	EXPECT_EQ(trace.Size(), 3u);
	EXPECT_EQ(trace.Loads(), 4u);
	EXPECT_EQ(trace.Count("_Prefabs/RawMaterials/PickupCotton", "UnityEngine.GameObject"), 2u);
	EXPECT_EQ(trace.Count("nothing", "UnityEngine.GameObject"), 0u);
	const auto entries = trace.Entries();
	ASSERT_EQ(entries.size(), 3u);
	EXPECT_EQ(entries[0].first, "UnityEngine.TextAsset\tREACTXLS/NGBalance/NGKnack/NGCarriableResources");
	EXPECT_EQ(entries[1].second, 2u);
}

TEST(ObjectsSettings, AreOffByDefaultAndReadFromTheFile)
{
	const auto defaults = moa_mod::loader::ParseLoaderSettings("{}");
	ASSERT_TRUE(defaults);
	EXPECT_FALSE(defaults->objects.enabled);
	EXPECT_TRUE(defaults->objects.trace);
	EXPECT_FALSE(defaults->objects.testClone);

	const auto settings = moa_mod::loader::ParseLoaderSettings(
	    R"({"objects": {"enabled": true, "trace": false, "traceFolder": "D:/x", "testClone": true, "packsFolder": "D:/p"}})");
	ASSERT_TRUE(settings);
	EXPECT_TRUE(settings->objects.enabled);
	EXPECT_FALSE(settings->objects.trace);
	EXPECT_EQ(settings->objects.traceFolder, "D:/x");
	EXPECT_TRUE(settings->objects.testClone);
	EXPECT_EQ(settings->objects.packsFolder, "D:/p");

	EXPECT_FALSE(moa_mod::loader::ParseLoaderSettings(R"({"objects": {"enabled": 1}})"));
	EXPECT_FALSE(moa_mod::loader::ParseLoaderSettings(R"({"objects": true})"));
}

TEST(ObjectsSettings, ChangeOnlyAfterARestart)
{
	moa_mod::loader::LoaderSettings current;
	auto next = current;
	next.objects.enabled = true;
	const auto change = moa_mod::loader::CompareLoaderSettings(current, next);
	EXPECT_FALSE(change.Any());
	EXPECT_EQ(change.restartOnly, (std::vector<std::string> {"objects"}));
	EXPECT_FALSE(moa_mod::loader::LiveLoaderSettings(current, next).objects.enabled);
	EXPECT_EQ(moa_mod::loader::LoaderSettingsJson(next)["objects"]["enabled"], true);
}
