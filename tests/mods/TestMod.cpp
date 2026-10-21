/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

// A small mod DLL for the tests of the loader. MOA_TEST_MOD_KIND selects what it does:
// 1: a correct mod that registers a settings section, a field, an action, a method callback and a query.
// 2: a mod whose init registers and then returns 0.
// 3: a mod of another interface version.
// 4: a mod whose init causes an access violation.
// 5: a correct mod with the same id as kind 1.
// 6: a mod with an id that is not permitted.

#include <cstdint>

#include <memory>
#include <string>

#include <moa_mod/api/ModApi.h>
#include <moa_mod/sdk/ModHost.h>

#ifndef MOA_TEST_MOD_KIND
#error "MOA_TEST_MOD_KIND is not set"
#endif

namespace
{
std::unique_ptr<moa_mod::sdk::ModHost> s_host;
double s_speed = 1.0;
int s_calls = 0;
int s_actions = 0;

[[nodiscard]] const char* ModId()
{
#if MOA_TEST_MOD_KIND == 6
	return "bad id!";
#elif MOA_TEST_MOD_KIND == 5 || MOA_TEST_MOD_KIND == 1
	return "test_mod";
#else
	return "test_mod_other";
#endif
}

uint32_t Init(const MoaHostApi* api, uint32_t apiVersion)
{
	if (!moa_mod::sdk::ModHost::Check(api, apiVersion))
	{
		return 0;
	}
	s_host = std::make_unique<moa_mod::sdk::ModHost>(*api, ModId());
	bool ok = s_host->AddSettings("testmod", [](const nlohmann::json& object) -> moa::Result<void> {
		s_speed = object.value("speed", 1.0);
		return {};
	});
	const MoaFieldDesc field {.menu = nullptr,
	                          .group = "Test",
	                          .section = "testmod",
	                          .key = "speed",
	                          .label = "Speed",
	                          .help = "A test value.",
	                          .kind = MOA_FIELD_NUMBER,
	                          .min = 0.0,
	                          .max = 10.0,
	                          .step = 0.1,
	                          .format = "%.1f",
	                          .logarithmic = 0,
	                          .restartOnly = 0,
	                          .choices = nullptr,
	                          .choiceCount = 0};
	ok = ok && s_host->AddField(
	               field, {.get =
	                           [] {
		                           return moa_mod::sdk::FieldState {
		                               .value = s_speed, .range = {.min = 0.0, .max = 10.0}, .restartPending = false};
	                           },
	                       .set = [](const moa_mod::settings::FieldValue& value, bool /*save*/) -> moa::Result<std::string> {
		                       s_speed = std::get<double>(value);
		                       return std::string("Speed changed");
	                       }});
	const MoaActionDesc action {.menu = nullptr,
	                            .group = "Test",
	                            .label = "Do it",
	                            .help = nullptr,
	                            .hotkey = "testAction",
	                            .defaultHotkey = "Ctrl+Shift+F2"};
	ok = ok && s_host->AddAction(action, [] { ++s_actions; });
	ok = ok && s_host->AddMethodCallback({.space = "", .className = "GameManager", .method = "Update", .parameterTypes = {}},
	                                     MOA_HOOK_AFTER, "count", [](const MoaCall& /*call*/) { ++s_calls; });
	ok = ok && s_host->AddQuery("test.speed", "The speed of the test mod.", [](const nlohmann::json& params) {
		return nlohmann::json {{"speed", s_speed + params.value("add", 0.0)}};
	});
#if MOA_TEST_MOD_KIND == 2
	return 0;
#elif MOA_TEST_MOD_KIND == 4
	volatile int* pointer = nullptr;
	*pointer = 1;
	return 1;
#else
	return ok ? 1 : 0;
#endif
}
} // namespace

extern "C" {
__declspec(dllexport) uint32_t MoaModApiVersion()
{
#if MOA_TEST_MOD_KIND == 3
	return MOA_MOD_API_VERSION + 1;
#else
	return MOA_MOD_API_VERSION;
#endif
}

__declspec(dllexport) const MoaModInfo* MoaModGetInfo()
{
	static const MoaModInfo info {.id = ModId(), .name = "Test mod", .version = "1.0"};
	return &info;
}

__declspec(dllexport) uint32_t MoaModInit(const MoaHostApi* api, uint32_t apiVersion)
{
	try
	{
		return Init(api, apiVersion);
	}
	catch (...)
	{
		return 0;
	}
}

__declspec(dllexport) void MoaModShutdown()
{
	s_host.reset();
}

/// For the test: the count of the calls of the method callback and of the action.
__declspec(dllexport) int TestModCalls()
{
	return s_calls * 1000 + s_actions;
}

__declspec(dllexport) double TestModSpeed()
{
	return s_speed;
}
}
