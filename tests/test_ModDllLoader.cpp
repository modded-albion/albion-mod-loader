/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <moa_mod/loader/HostTable.h>
#include <moa_mod/loader/ModDllLoader.h>
#include <windows.h>

using namespace moa_mod::loader;

namespace
{

/// The work of the loader for the tests: the registry, the log in memory and the tasks in a list.
class FakeServices final: public HostServices
{
public:
	ModRegistry& Registry() override { return registry; }
	void Log(OwnerId owner, MoaLogLevel /*level*/, std::string_view text) override
	{
		log.push_back(std::format("{}: {}", owner, text));
	}
	moa::Result<void> PostTask(OwnerId /*owner*/, MoaTaskFn task, void* user) override
	{
		tasks.emplace_back(task, user);
		return {};
	}
	moa::Result<void> SaveSettings(OwnerId /*owner*/, std::string_view patch) override
	{
		saved.emplace_back(patch);
		return {};
	}
	void ShowMessage(OwnerId /*owner*/, std::string_view text, double /*seconds*/) override { messages.emplace_back(text); }

	[[nodiscard]] bool Logged(std::string_view part) const
	{
		return std::ranges::any_of(log, [&](const std::string& line) { return line.find(part) != std::string::npos; });
	}

	ModRegistry registry;
	std::vector<std::string> log;
	std::vector<std::pair<MoaTaskFn, void*>> tasks;
	std::vector<std::string> saved;
	std::vector<std::string> messages;
};

[[nodiscard]] std::filesystem::path TestMod(const char* name)
{
	return std::filesystem::path(MOA_TEST_MODS_DIR) / name;
}

[[nodiscard]] HostInfo Info()
{
	return HostInfo {
	    .loaderVersion = "test", .buildGuid = "build", .gameFolder = "game", .settingsFolder = "settings", .logFolder = "logs"};
}

} // namespace

TEST(ModDllLoader, LoadsACorrectModAndKeepsItsRegistrations)
{
	FakeServices services;
	ModDllLoader loader(services, Info());
	const auto& mod = loader.Load(TestMod("moa_test_mod_good.dll"));
	ASSERT_EQ(mod.state, ModDllState::Loaded) << mod.error;
	EXPECT_EQ(mod.id, "test_mod");
	EXPECT_EQ(mod.name, "Test mod");
	EXPECT_EQ(mod.version, "1.0");
	ASSERT_EQ(services.registry.Settings().size(), 1u);
	ASSERT_EQ(services.registry.Fields().size(), 1u);
	ASSERT_EQ(services.registry.Actions().size(), 1u);
	ASSERT_EQ(services.registry.MethodCallbacks().size(), 1u);

	// The loader calls the mod through the registry, as the game side does.
	const auto& settings = services.registry.Settings()[0];
	EXPECT_EQ(settings.load(settings.user, R"({"speed": 4.5})", nullptr), 1u);
	const auto& field = services.registry.Fields()[0];
	MoaFieldState state {};
	field.get(field.user, &state);
	EXPECT_DOUBLE_EQ(state.value.number, 4.5);
	const auto& callback = services.registry.MethodCallbacks()[0];
	const MoaCall call {.instance = nullptr, .arguments = {}};
	callback.call(callback.user, &call);
	callback.call(callback.user, &call);
	const auto& action = services.registry.Actions()[0];
	action.run(action.user);
	using Calls = int (*)();
	const auto calls = reinterpret_cast<Calls>(GetProcAddress(static_cast<HMODULE>(mod.module), "TestModCalls"));
	ASSERT_NE(calls, nullptr);
	EXPECT_EQ(calls(), 2001);

	// After the init, the table refuses new registrations.
	const auto& api = mod.table->Api();
	EXPECT_EQ(api.addSettings(api.context, "late", settings.load, nullptr), 0u);
	EXPECT_TRUE(services.Logged("only in MoaModInit"));
	loader.ShutdownAll();
}

TEST(ModDllLoader, UnloadsAModWhoseInitFailsAndRemovesItsRegistrations)
{
	FakeServices services;
	ModDllLoader loader(services, Info());
	const auto& mod = loader.Load(TestMod("moa_test_mod_init_fails.dll"));
	EXPECT_EQ(mod.state, ModDllState::Refused);
	EXPECT_NE(mod.error.find("returned 0"), std::string::npos);
	EXPECT_EQ(mod.module, nullptr);
	EXPECT_TRUE(services.registry.Settings().empty());
	EXPECT_TRUE(services.registry.Fields().empty());
	EXPECT_TRUE(services.registry.MethodCallbacks().empty());
	// The section of the failed mod is free for the next mod.
	EXPECT_EQ(loader.Load(TestMod("moa_test_mod_good.dll")).state, ModDllState::Loaded);
	loader.ShutdownAll();
}

TEST(ModDllLoader, RefusesAModOfAnotherInterfaceVersion)
{
	FakeServices services;
	ModDllLoader loader(services, Info());
	const auto& mod = loader.Load(TestMod("moa_test_mod_old.dll"));
	EXPECT_EQ(mod.state, ModDllState::Refused);
	EXPECT_NE(mod.error.find("interface version"), std::string::npos);
	EXPECT_TRUE(services.registry.Settings().empty());
}

TEST(ModDllLoader, CatchesAnAccessViolationInTheInit)
{
	FakeServices services;
	ModDllLoader loader(services, Info());
	const auto& mod = loader.Load(TestMod("moa_test_mod_crash.dll"));
	EXPECT_EQ(mod.state, ModDllState::Failed);
	EXPECT_NE(mod.error.find("access violation"), std::string::npos);
	EXPECT_TRUE(services.registry.Settings().empty());
	EXPECT_TRUE(services.registry.MethodCallbacks().empty());
}

TEST(ModDllLoader, RefusesASecondModWithTheSameIdAndABadId)
{
	FakeServices services;
	ModDllLoader loader(services, Info());
	EXPECT_EQ(loader.Load(TestMod("moa_test_mod_good.dll")).state, ModDllState::Loaded);
	const auto& same = loader.Load(TestMod("moa_test_mod_same_id.dll"));
	EXPECT_EQ(same.state, ModDllState::Refused);
	EXPECT_NE(same.error.find("same id"), std::string::npos);
	const auto& bad = loader.Load(TestMod("moa_test_mod_bad_id.dll"));
	EXPECT_EQ(bad.state, ModDllState::Refused);
	EXPECT_EQ(loader.Mods().size(), 3u);
	EXPECT_EQ(services.registry.Settings().size(), 1u);
	loader.ShutdownAll();
}

TEST(ModDllLoader, RefusesADllThatIsNotAModAndAFileThatIsMissing)
{
	FakeServices services;
	ModDllLoader loader(services, Info());
	std::array<wchar_t, MAX_PATH> system {};
	const auto length = GetSystemDirectoryW(system.data(), static_cast<UINT>(system.size()));
	const auto version = std::filesystem::path(std::wstring(system.data(), length)) / "version.dll";
	const auto& plain = loader.Load(version);
	EXPECT_EQ(plain.state, ModDllState::Refused);
	EXPECT_NE(plain.error.find("not a mod"), std::string::npos);
	const auto& missing = loader.Load(TestMod("no_such_mod.dll"));
	EXPECT_EQ(missing.state, ModDllState::Refused);
	const auto& off = loader.AddDisabled(TestMod("off.dll"), "modDlls.disabled");
	EXPECT_EQ(off.state, ModDllState::Disabled);
	EXPECT_STREQ(ModDllStateName(off.state), "off");
}

TEST(ModDllLoader, FindsTheDllsOfAFolderInTheOrderOfTheirNames)
{
	const auto files = FindModDlls(MOA_TEST_MODS_DIR);
	ASSERT_GE(files.size(), 6u);
	EXPECT_TRUE(std::ranges::is_sorted(files, {}, [](const auto& path) { return path.filename().wstring(); }));
	EXPECT_TRUE(FindModDlls(TestMod("no_such_folder")).empty());
}

TEST(HostTable, GivesTheTextsAndLogsWithTheOwner)
{
	FakeServices services;
	HostTable table(services, 7, Info());
	const auto& api = table.Api();
	EXPECT_EQ(api.apiVersion, MOA_MOD_API_VERSION);
	EXPECT_EQ(api.size, sizeof(MoaHostApi));
	EXPECT_STREQ(api.buildGuid, "build");
	EXPECT_STREQ(api.logFolder, "logs");
	api.log(api.context, MOA_LOG_INFO, "hello");
	EXPECT_EQ(services.log.back(), "7: hello");
	EXPECT_EQ(api.saveSettings(api.context, R"({"a": 1})"), 1u);
	EXPECT_EQ(services.saved.back(), R"({"a": 1})");
	api.showMessage(api.context, "Camera mod: on", 2.0);
	EXPECT_EQ(services.messages.back(), "Camera mod: on");
	EXPECT_EQ(api.postTask(api.context, nullptr, nullptr), 0u);
	EXPECT_EQ(api.addMethodCallback(api.context, nullptr, MOA_HOOK_AFTER, "x", nullptr, nullptr), 0u);
	EXPECT_EQ(table.Refusals(), 2u);
}
