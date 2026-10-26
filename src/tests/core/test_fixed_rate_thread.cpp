#include <gtest/gtest.h>
#include "axeng/core/fixed_rate_thread.h"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace
{
	// A clock the test moves by hand.
	struct TestClock
	{
		std::atomic<double> seconds{ 100.0 };
		ax::FixedRateThread::Clock clock() { return [this] { return seconds.load(); }; }
	};

	// Waits (briefly) for the thread to reach `ticks`; returns the count it reached.
	std::uint64_t settle(ax::FixedRateThread& thread, std::uint64_t ticks)
	{
		for (int attempt{}; attempt < 200 && thread.ticks() < ticks; ++attempt)
		{
			thread.poke();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		// Give an over-eager thread the chance to overshoot before the caller checks the count.
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		return thread.ticks();
	}

	// The count after giving the thread time to run ticks it should not run.
	std::uint64_t quiet(ax::FixedRateThread& thread)
	{
		thread.poke();
		std::this_thread::sleep_for(std::chrono::milliseconds(40));
		return thread.ticks();
	}
}

TEST(FixedRateThread, RequestedModeRunsExactlyTheRequestedTicks)
{
	std::atomic<int> ran{};
	ax::FixedRateThread thread{ [&] { ran.fetch_add(1); } };
	thread.start(ax::FixedRateThread::Mode::Requested);
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	EXPECT_EQ(ran.load(), 0);
	thread.request(3);
	thread.wait_requested();
	EXPECT_EQ(ran.load(), 3);
	EXPECT_EQ(thread.ticks(), 3u);
	thread.request();
	thread.wait_requested();
	EXPECT_EQ(ran.load(), 4);
	thread.stop();
	EXPECT_FALSE(thread.running());
}

TEST(FixedRateThread, FreeModeFollowsTheClockWithBoundedCatchUp)
{
	TestClock clock{};
	ax::FixedRateThread thread{ [] {}, 60.0, 6, clock.clock() };
	thread.start(ax::FixedRateThread::Mode::Free);
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	EXPECT_EQ(thread.ticks(), 0u);
	clock.seconds = clock.seconds + 3.0 / 60.0;
	EXPECT_EQ(settle(thread, 3), 3u);
	// A whole second behind runs at most six ticks and reports the rest as discarded.
	clock.seconds = clock.seconds + 1.0;
	EXPECT_EQ(settle(thread, 9), 9u);
	EXPECT_NEAR(thread.discarded_seconds(), 1.0 - 6.0 / 60.0, 1e-9);
	thread.stop();
}

TEST(FixedRateThread, PausedTimeIsNotCaughtUp)
{
	TestClock clock{};
	ax::FixedRateThread thread{ [] {}, 60.0, 6, clock.clock() };
	thread.start(ax::FixedRateThread::Mode::Free);
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	clock.seconds = clock.seconds + 2.0 / 60.0;
	EXPECT_EQ(settle(thread, 2), 2u);
	thread.set_paused(true);
	clock.seconds = clock.seconds + 10.0;
	EXPECT_EQ(quiet(thread), 2u);
	thread.set_paused(false);
	EXPECT_EQ(quiet(thread), 2u);
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	clock.seconds = clock.seconds + 1.0 / 60.0;
	EXPECT_EQ(settle(thread, 3), 3u);
	EXPECT_DOUBLE_EQ(thread.discarded_seconds(), 0.0);
	thread.stop();
}

TEST(FixedRateThread, SwitchingToRequestedModeStopsFreeRunning)
{
	TestClock clock{};
	ax::FixedRateThread thread{ [] {}, 60.0, 6, clock.clock() };
	thread.start(ax::FixedRateThread::Mode::Free);
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	clock.seconds = clock.seconds + 1.0 / 60.0;
	EXPECT_EQ(settle(thread, 1), 1u);
	thread.set_mode(ax::FixedRateThread::Mode::Requested);
	clock.seconds = clock.seconds + 1.0;
	EXPECT_EQ(quiet(thread), 1u);
	thread.request(2);
	thread.wait_requested();
	EXPECT_EQ(thread.ticks(), 3u);
	thread.stop();
}

TEST(FixedRateThread, AFailedTickStopsTheThreadAndReachesTheCaller)
{
	std::atomic<int> ran{};
	ax::FixedRateThread thread{ [&]
	{
		if (ran.fetch_add(1) == 1) throw std::runtime_error{ "tick failed" };
	} };
	thread.start(ax::FixedRateThread::Mode::Requested);
	thread.request(5);
	EXPECT_THROW(thread.wait_requested(), std::runtime_error);
	EXPECT_EQ(ran.load(), 2);
	EXPECT_NO_THROW(thread.stop());
}
