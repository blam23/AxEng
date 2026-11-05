#include <gtest/gtest.h>

#include "axeng/core/render_profiler.h"

TEST(RenderProfiler, CountsCategoriesAndResetsEveryFrame)
{
	ax::RenderProfiler profiler;
	profiler.begin_frame();
	ASSERT_EQ(ax::RenderProfiler::current(), &profiler);
	profiler.record_write(ax::RenderCategory::Sprites, 48);
	profiler.record_write(ax::RenderCategory::Sprites, 96);
	profiler.record_write(ax::RenderCategory::Shapes, 128);
	profiler.record_draw(ax::RenderCategory::Sprites);
	profiler.record_packed(ax::RenderCategory::Sprites, 3);
	profiler.record_tessellation();
	profiler.record_growth();
	profiler.record_submit();
	profiler.end_frame();
	EXPECT_EQ(ax::RenderProfiler::current(), nullptr);
	const auto& stats{ profiler.statistics() };
	EXPECT_EQ(stats.frame, 1u);
	EXPECT_EQ(stats.submits, 1u);
	EXPECT_EQ(stats.tessellations, 1u);
	EXPECT_EQ(stats.bufferGrowths, 1u);
	EXPECT_EQ(stats.categories[0].writes, 2u);
	EXPECT_EQ(stats.categories[0].bytes, 144u);
	EXPECT_EQ(stats.categories[0].draws, 1u);
	EXPECT_EQ(stats.categories[0].packed, 3u);
	EXPECT_EQ(stats.categories[2].bytes, 128u);
	profiler.begin_frame();
	EXPECT_EQ(profiler.statistics().frame, 2u);
	EXPECT_EQ(profiler.statistics().categories[0].writes, 0u);
	EXPECT_EQ(profiler.statistics().submits, 0u);
	profiler.end_frame();
}

TEST(RenderProfiler, DisabledCountersAndUnsupportedTimestampsRemainEmpty)
{
	ax::RenderProfiler profiler;
	profiler.configure(false, true);
	profiler.begin_frame();
	profiler.record_write(ax::RenderCategory::Pending, 64);
	profiler.record_draw(ax::RenderCategory::Pending);
	profiler.record_packed(ax::RenderCategory::Pending, 1);
	profiler.record_cpu(ax::RenderCategory::Pending, 2.0);
	profiler.record_submit();
	profiler.record_tessellation();
	profiler.record_growth();
	profiler.end_frame();
	EXPECT_FALSE(profiler.statistics().enabled);
	EXPECT_EQ(profiler.statistics().categories[1].bytes, 0u);
	EXPECT_EQ(profiler.statistics().categories[1].draws, 0u);
	EXPECT_EQ(profiler.statistics().categories[1].packed, 0u);
	EXPECT_EQ(profiler.statistics().categories[1].cpuMilliseconds, 0.0);
	EXPECT_EQ(profiler.statistics().submits, 0u);
	EXPECT_EQ(profiler.statistics().tessellations, 0u);
	EXPECT_EQ(profiler.statistics().bufferGrowths, 0u);
	EXPECT_FALSE(profiler.gpu_statistics().supported);
	EXPECT_FALSE(profiler.gpu_statistics().enabled);
	EXPECT_FALSE(profiler.gpu_statistics().valid);
}
