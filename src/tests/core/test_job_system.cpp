#include <gtest/gtest.h>
#include "axeng/core/job_system.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace
{
	std::vector<std::pair<std::size_t, std::size_t>> chunks_of(ax::JobSystem& jobs, std::size_t count, std::size_t grain)
	{
		std::mutex mutex{};
		std::vector<std::pair<std::size_t, std::size_t>> chunks{};
		jobs.parallel_for(count, grain, [&](std::size_t begin, std::size_t end)
		{
			std::lock_guard lock{ mutex };
			chunks.emplace_back(begin, end);
		});
		std::sort(chunks.begin(), chunks.end());
		return chunks;
	}
}

TEST(JobSystem, ParallelForVisitsEveryIndexExactlyOnce)
{
	ax::JobSystem jobs{ 3 };
	std::vector<std::atomic<int>> visits(10007);
	jobs.parallel_for(visits.size(), 64, [&](std::size_t begin, std::size_t end)
	{
		for (auto index{ begin }; index < end; ++index) visits[index].fetch_add(1);
	});
	for (const auto& visit : visits) ASSERT_EQ(visit.load(), 1);
}

TEST(JobSystem, ChunkBoundariesDependOnlyOnCountAndGrain)
{
	ax::JobSystem none{ 0 }, few{ 2 }, many{ 7 };
	const auto expected{ chunks_of(none, 1000, 37) };
	ASSERT_EQ(expected.size(), 28u);
	EXPECT_EQ(expected.front(), (std::pair<std::size_t, std::size_t>{ 0, 37 }));
	EXPECT_EQ(expected.back(), (std::pair<std::size_t, std::size_t>{ 999, 1000 }));
	EXPECT_EQ(chunks_of(few, 1000, 37), expected);
	EXPECT_EQ(chunks_of(many, 1000, 37), expected);
}

TEST(JobSystem, WithoutWorkersEverythingRunsOnTheCallingThread)
{
	ax::JobSystem jobs{ 0 };
	EXPECT_EQ(jobs.worker_count(), 0u);
	const auto caller{ std::this_thread::get_id() };
	bool sameThread{ true };
	ax::JobSystem::Group group{ jobs };
	for (int task{}; task < 8; ++task)
		group.run([&] { sameThread = sameThread && std::this_thread::get_id() == caller; });
	jobs.parallel_for(100, 3, [&](std::size_t, std::size_t) { sameThread = sameThread && std::this_thread::get_id() == caller; });
	group.wait();
	EXPECT_TRUE(sameThread);
}

TEST(JobSystem, WorkersShareTheWork)
{
	ax::JobSystem jobs{ 3 };
	std::mutex mutex{};
	std::set<std::thread::id> threads{};
	std::atomic<int> started{};
	ax::JobSystem::Group group{ jobs };
	for (int task{}; task < 4; ++task)
		group.run([&]
		{
			{
				std::lock_guard lock{ mutex };
				threads.insert(std::this_thread::get_id());
			}
			// Hold every task until all four are running, which needs four threads (three workers and the waiter).
			started.fetch_add(1);
			while (started.load() < 4) std::this_thread::yield();
		});
	group.wait();
	EXPECT_EQ(threads.size(), 4u);
}

TEST(JobSystem, NestedGroupsDoNotDeadlockWhenEveryThreadWaits)
{
	ax::JobSystem jobs{ 2 };
	std::atomic<int> leaves{};
	ax::JobSystem::Group outer{ jobs };
	for (int branch{}; branch < 8; ++branch)
		outer.run([&]
		{
			ax::JobSystem::Group inner{ jobs };
			for (int leaf{}; leaf < 8; ++leaf) inner.run([&] { leaves.fetch_add(1); });
			inner.wait();
			jobs.parallel_for(16, 1, [&](std::size_t, std::size_t) { leaves.fetch_add(1); });
		});
	outer.wait();
	EXPECT_EQ(leaves.load(), 8 * (8 + 16));
}

TEST(JobSystem, ExceptionsReachTheWaitingThread)
{
	ax::JobSystem jobs{ 2 };
	ax::JobSystem::Group group{ jobs };
	std::atomic<int> ran{};
	for (int task{}; task < 6; ++task)
		group.run([&, task]
		{
			ran.fetch_add(1);
			if (task == 3) throw std::runtime_error{ "task failed" };
		});
	EXPECT_THROW(group.wait(), std::runtime_error);
	EXPECT_EQ(ran.load(), 6);
	EXPECT_THROW(jobs.parallel_for(100, 1, [](std::size_t begin, std::size_t)
	{
		if (begin == 42) throw std::logic_error{ "chunk failed" };
	}), std::logic_error);
	// The pool keeps working after a failure.
	std::atomic<int> after{};
	jobs.parallel_for(10, 1, [&](std::size_t, std::size_t) { after.fetch_add(1); });
	EXPECT_EQ(after.load(), 10);
}

TEST(JobSystem, EmptyAndSingleChunkLoopsRunInline)
{
	ax::JobSystem jobs{ 2 };
	int calls{};
	jobs.parallel_for(0, 8, [&](std::size_t, std::size_t) { ++calls; });
	EXPECT_EQ(calls, 0);
	jobs.parallel_for(5, 8, [&](std::size_t begin, std::size_t end)
	{
		++calls;
		EXPECT_EQ(begin, 0u);
		EXPECT_EQ(end, 5u);
	});
	EXPECT_EQ(calls, 1);
	jobs.parallel_for(5, 0, [&](std::size_t begin, std::size_t end) { calls += static_cast<int>(end - begin); });
	EXPECT_EQ(calls, 6);
}
