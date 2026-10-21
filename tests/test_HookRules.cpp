/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cstdint>

#include <array>

#include <core/HookRules.h>
#include <gtest/gtest.h>

using namespace moa_mod::overlay;

namespace
{
constexpr std::array<uintptr_t, 2> k_OwnHooks {0x1000, 0x2000};
} // namespace

TEST(HookRules, AcceptsAFunctionOfTheVtableModule)
{
	EXPECT_EQ(CheckHookTarget(0x7000, k_OwnHooks, true), HookTargetError::None);
}

TEST(HookRules, RefusesAnEmptySlot)
{
	EXPECT_EQ(CheckHookTarget(0, k_OwnHooks, true), HookTargetError::NoFunction);
}

TEST(HookRules, NeverKeepsAHookOfTheModAsTheOriginal)
{
	// A slot that holds a hook of the mod must never become the original function, also in the module of the vtable.
	EXPECT_EQ(CheckHookTarget(0x1000, k_OwnHooks, true), HookTargetError::OwnHook);
	EXPECT_EQ(CheckHookTarget(0x2000, k_OwnHooks, false), HookTargetError::OwnHook);
}

TEST(HookRules, RefusesAFunctionOutsideTheVtableModule)
{
	EXPECT_EQ(CheckHookTarget(0x7000, k_OwnHooks, false), HookTargetError::OutsideModule);
}

TEST(HookRules, OnlyTheOuterCallIsOuter)
{
	HookDepth depth;
	{
		const HookScope outer(depth);
		EXPECT_TRUE(outer.Outer());
		EXPECT_EQ(depth.Depth(), 1);
		{
			const HookScope inner(depth);
			EXPECT_FALSE(inner.Outer());
			EXPECT_EQ(depth.Depth(), 2);
		}
		EXPECT_EQ(depth.Depth(), 1);
	}
	EXPECT_EQ(depth.Depth(), 0);
	const HookScope next(depth);
	EXPECT_TRUE(next.Outer());
}

TEST(HookRules, FindsALoopOneTime)
{
	HookDepth depth;
	int loops = 0;
	for (int call = 1; call <= k_HookLoopDepth + 4; ++call)
	{
		const auto entry = depth.Enter();
		EXPECT_EQ(entry.outer, call == 1);
		loops += entry.loopFound ? 1 : 0;
		EXPECT_EQ(entry.loopFound, call == k_HookLoopDepth);
	}
	for (int call = 0; call < k_HookLoopDepth + 4; ++call)
	{
		depth.Leave();
	}
	EXPECT_EQ(depth.Depth(), 0);
	EXPECT_EQ(loops, 1);
	// A second loop on the same thread is not told again.
	for (int call = 0; call < k_HookLoopDepth; ++call)
	{
		EXPECT_FALSE(depth.Enter().loopFound);
	}
}

TEST(HookRules, LeaveNeverGoesBelowZero)
{
	HookDepth depth;
	depth.Leave();
	EXPECT_EQ(depth.Depth(), 0);
	EXPECT_TRUE(depth.Enter().outer);
}

TEST(HookRules, ANormalChainHasNoLoop)
{
	// A hook on Present that DXGI calls one more time (for example from another function of the swap chain) is a
	// depth of two. It is not a loop.
	HookDepth depth;
	EXPECT_FALSE(depth.Enter().loopFound);
	EXPECT_FALSE(depth.Enter().loopFound);
	depth.Leave();
	depth.Leave();
	EXPECT_EQ(depth.Depth(), 0);
}
