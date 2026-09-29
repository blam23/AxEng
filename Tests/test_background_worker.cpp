#include <gtest/gtest.h>
#include "AxEngLib/background_worker.h"

#include <atomic>
#include <chrono>
#include <future>

using namespace ax;

namespace
{
	using ResultPtr = std::unique_ptr<BackgroundTaskResult>;
	constexpr auto callbackTimeout = std::chrono::seconds(5);
}

TEST(BackgroundWorkerTests, ExecutesTaskAndCallsSuccessCallback)
{
	BackgroundWorker worker{ false, {} };
	std::promise<ResultPtr> callbackPromise;
	auto callbackFuture = callbackPromise.get_future();
	std::atomic<int> taskRuns{ 0 };

	auto* task = worker.create();
	task->set_task([&]()
	{
		++taskRuns;
		return Error::Success;
	});
	task->set_success([&](ResultPtr result) { callbackPromise.set_value(std::move(result)); });

	worker.start({});
	worker.enqueue(task);

	ASSERT_EQ(callbackFuture.wait_for(callbackTimeout), std::future_status::ready);
	auto result = callbackFuture.get();
	ASSERT_NE(result, nullptr);
	EXPECT_TRUE(result->is_valid());
	EXPECT_EQ(result->get_error(), Error::Success);
	worker.stop();
	EXPECT_EQ(taskRuns.load(), 1);
}

TEST(BackgroundWorkerTests, CallsFailureCallbackWithTaskError)
{
	BackgroundWorker worker{ false, {} };
	std::promise<ResultPtr> callbackPromise;
	auto callbackFuture = callbackPromise.get_future();

	auto* task = worker.create();
	task->set_task([]() { return Error::GenericFailure; });
	task->set_failure([&](ResultPtr result) { callbackPromise.set_value(std::move(result)); });

	worker.start({});
	worker.enqueue(task);

	ASSERT_EQ(callbackFuture.wait_for(callbackTimeout), std::future_status::ready);
	auto result = callbackFuture.get();
	ASSERT_NE(result, nullptr);
	EXPECT_TRUE(result->is_valid());
	EXPECT_EQ(result->get_error(), Error::GenericFailure);
	worker.stop();
}

TEST(BackgroundWorkerTests, ProcessesMultipleQueuedTasks)
{
	BackgroundWorker worker{ false, {} };
	std::promise<ResultPtr> firstCallbackPromise;
	std::promise<ResultPtr> secondCallbackPromise;
	auto firstCallbackFuture = firstCallbackPromise.get_future();
	auto secondCallbackFuture = secondCallbackPromise.get_future();
	std::atomic<int> taskRuns{ 0 };

	auto* firstTask = worker.create();
	firstTask->set_task([&]()
	{
		++taskRuns;
		return Error::Success;
	});
	firstTask->set_success([&](ResultPtr result) { firstCallbackPromise.set_value(std::move(result)); });

	auto* secondTask = worker.create();
	secondTask->set_task([&]()
	{
		++taskRuns;
		return Error::Success;
	});
	secondTask->set_success([&](ResultPtr result) { secondCallbackPromise.set_value(std::move(result)); });

	worker.start({});
	worker.enqueue(firstTask);
	worker.enqueue(secondTask);

	ASSERT_EQ(firstCallbackFuture.wait_for(callbackTimeout), std::future_status::ready);
	ASSERT_EQ(secondCallbackFuture.wait_for(callbackTimeout), std::future_status::ready);
	auto firstResult = firstCallbackFuture.get();
	auto secondResult = secondCallbackFuture.get();
	ASSERT_NE(firstResult, nullptr);
	ASSERT_NE(secondResult, nullptr);
	EXPECT_EQ(firstResult->get_error(), Error::Success);
	EXPECT_EQ(secondResult->get_error(), Error::Success);
	worker.stop();
	EXPECT_EQ(taskRuns.load(), 2);
}
