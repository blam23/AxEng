#include <gtest/gtest.h>
#include "axeng/core/fixed_update_scheduler.h"

TEST(FixedUpdateScheduler, WaitsForDeadlineAndNeverRendersUnchangedUpdates)
{
	ax::FixedUpdateScheduler scheduler{};
	EXPECT_FALSE(scheduler.render_due());
	EXPECT_EQ(scheduler.advance(0.010), 0u);
	EXPECT_NEAR(scheduler.wait_seconds(), 1.0 / 60.0 - 0.010, 1e-12);
	EXPECT_EQ(scheduler.advance(1.0 / 60.0 - 0.010), 1u);
	scheduler.complete_update();
	EXPECT_TRUE(scheduler.render_due());
	scheduler.complete_render();
	EXPECT_FALSE(scheduler.render_due());
}

TEST(FixedUpdateScheduler, SlowFramesCoalesceWithoutLosingSimulationTicks)
{
	ax::FixedUpdateScheduler scheduler{};
	for (int frame{}; frame < 30; ++frame)
	{
		const auto ticks{ scheduler.advance(1.0 / 30.0) };
		ASSERT_EQ(ticks, 2u);
		for (std::uint32_t tick{}; tick < ticks; ++tick) scheduler.complete_update();
		EXPECT_TRUE(scheduler.render_due());
		scheduler.complete_render();
		EXPECT_FALSE(scheduler.render_due());
	}
	EXPECT_EQ(scheduler.updates(), 60u);
	EXPECT_DOUBLE_EQ(scheduler.discarded_seconds(), 0.0);
}

TEST(FixedUpdateScheduler, BoundsOverloadAndPreservesFractionalRemainder)
{
	ax::FixedUpdateScheduler scheduler{};
	EXPECT_EQ(scheduler.advance(10.5 / 60.0), 6u);
	EXPECT_NEAR(scheduler.discarded_seconds(), 4.0 / 60.0, 1e-12);
	EXPECT_NEAR(scheduler.wait_seconds(), 0.5 / 60.0, 1e-12);
	EXPECT_EQ(scheduler.advance(0.5 / 60.0), 1u);
}

TEST(FixedUpdateScheduler, FailedPresentationKeepsLatestUpdateEligible)
{
	ax::FixedUpdateScheduler scheduler{};
	scheduler.complete_update();
	// A failed surface acquisition does not call complete_render.
	EXPECT_TRUE(scheduler.render_due());
	scheduler.complete_update();
	scheduler.complete_render();
	EXPECT_FALSE(scheduler.render_due());
	EXPECT_EQ(scheduler.updates(), 2u);
}

TEST(FixedUpdateScheduler, RestoreStartsWithFreshDeadline)
{
	ax::FixedUpdateScheduler scheduler{};
	EXPECT_EQ(scheduler.advance(0.010), 0u);
	scheduler.reset_timing();
	EXPECT_EQ(scheduler.advance(0.010), 0u);
	EXPECT_DOUBLE_EQ(scheduler.discarded_seconds(), 0.0);
}

TEST(FixedUpdateScheduler, LongRunRetainsFractionalDeadlinesWithoutExtraTicks)
{
	ax::FixedUpdateScheduler scheduler{};
	std::uint64_t ticks{};
	for (int sample{}; sample < 144000; ++sample) ticks += scheduler.advance(1.0 / 144.0);
	EXPECT_EQ(ticks, 60000u);
	EXPECT_NEAR(scheduler.wait_seconds(), 1.0 / 60.0, 1e-9);
	EXPECT_DOUBLE_EQ(scheduler.discarded_seconds(), 0.0);
}
