/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <array>
#include <map>
#include <string>
#include <vector>

#include <core/ModRegistry.h>
#include <gtest/gtest.h>

using namespace moa_mod;
using namespace moa_mod::loader;

namespace
{

void Call(void* /*user*/, const MoaCall* /*call*/) {}
void Task(void* /*user*/) {}
uint32_t Load(void* /*user*/, const char* /*json*/, MoaText* /*error*/)
{
	return 1;
}
uint32_t Query(void* /*user*/, const char* /*params*/, MoaText* /*result*/)
{
	return 1;
}
void Get(void* /*user*/, MoaFieldState* /*state*/) {}
uint32_t Set(void* /*user*/, const MoaFieldValue* /*value*/, uint32_t /*save*/, MoaText* /*message*/)
{
	return 1;
}

const MethodKey k_Update {.space = "", .className = "GameManager", .method = "Update", .parameterTypes = {}};

[[nodiscard]] MoaFieldDesc Toggle(const char* section, const char* key)
{
	return MoaFieldDesc {.menu = nullptr,
	                     .group = "G",
	                     .section = section,
	                     .key = key,
	                     .label = "Label",
	                     .help = "Help",
	                     .kind = MOA_FIELD_TOGGLE,
	                     .min = 0.0,
	                     .max = 0.0,
	                     .step = 0.0,
	                     .format = nullptr,
	                     .logarithmic = 0,
	                     .restartOnly = 0,
	                     .choices = nullptr,
	                     .choiceCount = 0};
}

[[nodiscard]] MoaActionDesc ActionDesc(const char* name, const char* key)
{
	return MoaActionDesc {
	    .menu = nullptr, .group = "G", .label = nullptr, .help = nullptr, .hotkey = name, .defaultHotkey = key};
}

} // namespace

TEST(ModRegistry, TwoModsCanHaveCallbacksOnOneMethodButNotTheSameNameTwice)
{
	ModRegistry registry;
	EXPECT_TRUE(registry.AddMethodCallback(1, k_Update, MOA_HOOK_AFTER, "camera", &Call, nullptr));
	EXPECT_TRUE(registry.AddMethodCallback(2, k_Update, MOA_HOOK_AFTER, "camera", &Call, nullptr));
	EXPECT_TRUE(registry.AddMethodCallback(1, k_Update, MOA_HOOK_BEFORE, "camera", &Call, nullptr));
	EXPECT_FALSE(registry.AddMethodCallback(1, k_Update, MOA_HOOK_AFTER, "camera", &Call, nullptr));
	EXPECT_FALSE(registry.AddMethodCallback(1, k_Update, MOA_HOOK_AFTER, "", &Call, nullptr));
	EXPECT_FALSE(registry.AddMethodCallback(1, k_Update, MOA_HOOK_AFTER, "x", nullptr, nullptr));
	EXPECT_EQ(registry.MethodCallbacks().size(), 3u);
	EXPECT_EQ(registry.MethodsOf(1).size(), 1u);
}

TEST(ModRegistry, AllCallbacksOnAMethodAgreeOnItsParameters)
{
	ModRegistry registry;
	auto render = MethodKey {.space = "",
	                         .className = "EffectCameraShake",
	                         .method = "OnBeginCameraRendering",
	                         .parameterTypes = {"UnityEngine.Rendering.ScriptableRenderContext", "UnityEngine.Camera"}};
	EXPECT_TRUE(registry.AddMethodCallback(1, render, MOA_HOOK_BEFORE, "trace", &Call, nullptr));
	auto other = render;
	other.parameterTypes = {"A", "B"};
	EXPECT_FALSE(registry.AddMethodCallback(2, other, MOA_HOOK_BEFORE, "trace", &Call, nullptr));
	other.parameterTypes = {"A"};
	EXPECT_FALSE(registry.AddMethodCallback(2, other, MOA_HOOK_BEFORE, "one", &Call, nullptr));
	EXPECT_EQ(render.Text(),
	          "EffectCameraShake.OnBeginCameraRendering(UnityEngine.Rendering.ScriptableRenderContext, UnityEngine.Camera)");
}

TEST(ModRegistry, ASettingsSectionHasOneOwnerAndTheLoaderKeysStayWithTheLoader)
{
	ModRegistry registry;
	registry.SetOwnerName(1, "Camera");
	EXPECT_TRUE(registry.AddSettings(1, "camera", &Load, nullptr));
	const auto twice = registry.AddSettings(2, "camera", &Load, nullptr);
	ASSERT_FALSE(twice);
	EXPECT_NE(twice.GetError().message.find("Camera"), std::string::npos);
	EXPECT_FALSE(registry.AddSettings(2, "hotkeys", &Load, nullptr));
	EXPECT_FALSE(registry.AddSettings(2, "modDlls", &Load, nullptr));
	EXPECT_FALSE(registry.AddSettings(2, "bad name", &Load, nullptr));
	EXPECT_FALSE(registry.AddSettings(2, "", &Load, nullptr));
}

TEST(ModRegistry, AFieldMustBeInASectionOfItsOwner)
{
	ModRegistry registry;
	ASSERT_TRUE(registry.AddSettings(1, "camera", &Load, nullptr));
	const auto id = registry.AddField(1, Toggle("camera", "enabled"), &Get, &Set, nullptr);
	ASSERT_TRUE(id);
	EXPECT_EQ(registry.FindField(*id)->Path(), "camera.enabled");
	EXPECT_FALSE(registry.AddField(1, Toggle("camera", "enabled"), &Get, &Set, nullptr));
	EXPECT_FALSE(registry.AddField(2, Toggle("camera", "other"), &Get, &Set, nullptr));
	EXPECT_FALSE(registry.AddField(1, Toggle("moon", "enabled"), &Get, &Set, nullptr));
	EXPECT_TRUE(registry.AddField(k_LoaderOwner, Toggle("overlay", "enabled"), &Get, &Set, nullptr));
	EXPECT_TRUE(registry.AddField(k_LoaderOwner, Toggle("", "frameHook"), &Get, &Set, nullptr));

	auto choice = Toggle("camera", "mode");
	choice.kind = MOA_FIELD_CHOICE;
	EXPECT_FALSE(registry.AddField(1, choice, &Get, &Set, nullptr));
	const std::array<const char*, 2> names {"hand", "orbit"};
	choice.choices = names.data();
	choice.choiceCount = 2;
	const auto chosen = registry.AddField(1, choice, &Get, &Set, nullptr);
	ASSERT_TRUE(chosen);
	EXPECT_EQ(registry.FindField(*chosen)->choices, (std::vector<std::string> {"hand", "orbit"}));
}

TEST(ModRegistry, RemoveOwnerRemovesAllItsRegistrations)
{
	ModRegistry registry;
	ASSERT_TRUE(registry.AddSettings(1, "camera", &Load, nullptr));
	ASSERT_TRUE(registry.AddField(1, Toggle("camera", "enabled"), &Get, &Set, nullptr));
	ASSERT_TRUE(registry.AddAction(1, ActionDesc("toggleCamera", "Ctrl+Shift+F5"), &Task, nullptr, nullptr));
	ASSERT_TRUE(registry.AddMethodCallback(1, k_Update, MOA_HOOK_AFTER, "camera", &Call, nullptr));
	ASSERT_TRUE(registry.SetStatusReader(1, [](void*, MoaText*) {}, nullptr));
	ASSERT_TRUE(registry.AddSettings(2, "sky", &Load, nullptr));
	registry.RemoveOwner(1);
	EXPECT_TRUE(registry.Fields().empty());
	EXPECT_TRUE(registry.Actions().empty());
	EXPECT_TRUE(registry.MethodCallbacks().empty());
	EXPECT_TRUE(registry.Statuses().empty());
	ASSERT_EQ(registry.Settings().size(), 1u);
	// The section is free again.
	EXPECT_TRUE(registry.AddSettings(3, "camera", &Load, nullptr));
	registry.SetFailed(2, "an access violation");
	EXPECT_TRUE(registry.Failed(2));
	EXPECT_EQ(registry.FailReason(2), "an access violation");
	EXPECT_FALSE(registry.Failed(3));
}

TEST(ModRegistry, AHotkeyNameHasOneOwnerAndTheFileReplacesTheDefault)
{
	ModRegistry registry;
	ASSERT_TRUE(registry.AddAction(1, ActionDesc("toggleCamera", "Ctrl+Shift+F5"), &Task, nullptr, nullptr));
	ASSERT_TRUE(registry.AddAction(1, ActionDesc("resetView", "F5"), &Task, nullptr, nullptr));
	ASSERT_TRUE(registry.AddAction(2, ActionDesc("toggleMoon", "Ctrl+Shift+F5"), &Task, nullptr, nullptr));
	EXPECT_FALSE(registry.AddAction(2, ActionDesc("toggleCamera", "F7"), &Task, nullptr, nullptr));
	EXPECT_FALSE(registry.AddAction(2, ActionDesc("enabled", "F7"), &Task, nullptr, nullptr));
	EXPECT_FALSE(registry.AddAction(2, ActionDesc("other", "Meta+F7"), &Task, nullptr, nullptr));

	std::vector<std::string> errors;
	auto bindings = registry.ResolveHotkeys({}, errors);
	ASSERT_EQ(bindings.size(), 3u);
	// The later action with the same keys is off.
	EXPECT_EQ(FormatHotkey(bindings[0].hotkey), "Ctrl+Shift+F5");
	EXPECT_FALSE(bindings[2].hotkey.Enabled());
	ASSERT_EQ(errors.size(), 1u);

	errors.clear();
	const std::map<std::string, moa_mod::Hotkey> overrides {{"toggleMoon", *ParseHotkey("Ctrl+Shift+F6")},
	                                                        {"noSuchAction", *ParseHotkey("F9")}};
	bindings = registry.ResolveHotkeys(overrides, errors);
	EXPECT_EQ(FormatHotkey(bindings[2].hotkey), "Ctrl+Shift+F6");
	ASSERT_EQ(errors.size(), 1u);
	EXPECT_NE(errors[0].find("noSuchAction"), std::string::npos);
	EXPECT_EQ(registry.FindActionByHotkey("resetView")->owner, 1u);
}

TEST(ModRegistry, AQueryNameHasOneOwnerAndAnOwnerRemovalRemovesItsQueries)
{
	ModRegistry registry;
	registry.SetOwnerName(1, "Sky");
	ASSERT_TRUE(registry.AddQuery(1, "sky.state", "The state of the sky.", &Query, nullptr));
	ASSERT_TRUE(registry.AddQuery(2, "camera.state", "", &Query, nullptr));
	const auto taken = registry.AddQuery(2, "sky.state", "", &Query, nullptr);
	ASSERT_FALSE(taken);
	EXPECT_NE(taken.GetError().message.find("belongs to Sky"), std::string::npos);
	EXPECT_FALSE(registry.AddQuery(2, "", "", &Query, nullptr));
	EXPECT_FALSE(registry.AddQuery(2, "1st", "", &Query, nullptr));
	EXPECT_FALSE(registry.AddQuery(2, "bad name", "", &Query, nullptr));
	EXPECT_FALSE(registry.AddQuery(2, "no.function", "", nullptr, nullptr));
	ASSERT_NE(registry.FindQuery("sky.state"), nullptr);
	EXPECT_EQ(registry.FindQuery("sky.state")->help, "The state of the sky.");
	EXPECT_EQ(registry.FindQuery("sky.other"), nullptr);
	registry.RemoveOwner(1);
	EXPECT_EQ(registry.FindQuery("sky.state"), nullptr);
	ASSERT_EQ(registry.Queries().size(), 1u);
	EXPECT_EQ(registry.Queries()[0].name, "camera.state");
}
