#include <gtest/gtest.h>
#include "AxEngLib/background_worker.h"
#include "AxEngLib/application.h"
#include "AxEngLib/resource_loader.h"

#include "log_capture.h"

#include <atomic>
#include <chrono>
#include <future>

using namespace ax;

namespace
{
	using ResultPtr = std::unique_ptr<BackgroundTaskResult>;
	constexpr auto callbackTimeout = std::chrono::seconds(5);

	void register_worker_lua_resources()
	{
		static constexpr char initCode[] = "app = {}";
		static constexpr char stdCode[] = "";
		Resource::register_embedded_resource<lua::Script>("@init", EmbeddedResource
		{
			reinterpret_cast<const uint8_t*>(initCode), sizeof(initCode) - 1
		});
		Resource::register_embedded_resource<lua::Script>("@std", EmbeddedResource
		{
			reinterpret_cast<const uint8_t*>(stdCode), sizeof(stdCode) - 1
		});
	}

	Application make_worker_app(const char* code, size_t size)
	{
		EmbeddedResourceLayout layout;
		layout.emplace("worker.lua", EmbeddedResource
		{
			reinterpret_cast<const uint8_t*>(code), size
		});
		return Application::from_embedded({}, std::move(layout));
	}
}

TEST(BackgroundWorkerTests, RunsLuaScriptTaskWhenLuaIsEnabled)
{
	register_worker_lua_resources();
	static constexpr char scriptCode[] = "assert(args[1] == 'worker-arg')";

	Application app = make_worker_app(scriptCode, sizeof(scriptCode) - 1);
	BackgroundWorker worker{ true, {} };
	ASSERT_EQ(worker.lua().setup(), Error::Success);
	std::promise<ResultPtr> callbackPromise;
	auto callbackFuture = callbackPromise.get_future();
	auto* script = app.scripts().load("worker_test", "worker.lua");
	ASSERT_NE(script, nullptr);

	auto* task = worker.create_script_task(script);
	ASSERT_NE(task, nullptr);
	task->set_success([&](ResultPtr result) { callbackPromise.set_value(std::move(result)); });

	worker.start({ "worker-arg" });
	worker.enqueue(task);
	ASSERT_EQ(callbackFuture.wait_for(callbackTimeout), std::future_status::ready);
	auto result = callbackFuture.get();
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result->get_error(), Error::Success);
	worker.stop();
} 

TEST(BackgroundWorkerTests, ReportsLuaScriptTaskRuntimeErrors)
{
	register_worker_lua_resources();
	testlog::LogCapture::instance().expect_total_error_count(1);
	static constexpr char scriptCode[] = "error('expected worker script failure')";

	Application app = make_worker_app(scriptCode, sizeof(scriptCode) - 1);
	BackgroundWorker worker{ true, {} };
	ASSERT_EQ(worker.lua().setup(), Error::Success);
	std::promise<ResultPtr> callbackPromise;
	auto callbackFuture = callbackPromise.get_future();
	auto* script = app.scripts().load("worker_test", "worker.lua");
	ASSERT_NE(script, nullptr);

	auto* task = worker.create_script_task(script);
	ASSERT_NE(task, nullptr);
	task->set_failure([&](ResultPtr result) { callbackPromise.set_value(std::move(result)); });

	worker.start({});
	worker.enqueue(task);
	ASSERT_EQ(callbackFuture.wait_for(callbackTimeout), std::future_status::ready);
	auto result = callbackFuture.get();
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result->get_error(), Error::Lua);
	worker.stop();
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
