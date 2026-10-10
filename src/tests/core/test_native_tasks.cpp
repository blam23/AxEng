#include <gtest/gtest.h>

#include "axeng/core/native_tasks.h"
#include "axeng/core/lua/bindings/lua_noise_bindings.h"
#include "axeng/core/lua/bindings/lua_vector_bindings.h"
#include "tests/main/log_capture.h"

#include <chrono>
#include <cmath>
#include <future>

namespace
{
	constexpr unsigned char s_std[]{
		#embed "../../axeng/core/lua/external/std.lua"
	};
	constexpr unsigned char s_generator[]{
		#embed "../../../tower_mancer/scripts/generate_chunk.lua"
	};

	std::string text(const unsigned char* data, std::size_t size)
	{
		return { reinterpret_cast<const char*>(data), size };
	}

	bool await_task(const std::shared_ptr<ax::NativeTask>& task)
	{
		const auto deadline{ std::chrono::steady_clock::now() + std::chrono::seconds(5) };
		while (task->status() == "queued" || task->status() == "running")
		{
			if (std::chrono::steady_clock::now() >= deadline)
				return false;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return true;
	}
}

class NativeTaskTests : public testing::Test
{
protected:
	void start(std::map<std::string, std::string> sources)
	{
		sources.emplace("@std", text(s_std, sizeof(s_std)));
		m_service = std::make_unique<ax::NativeTaskService>(flag_set<ax::lua::Permission>{}, std::move(sources));
		m_worker.set_thread_hooks([this]() { m_service->initialize(); }, [this]() { m_service->finalize(); });
		m_worker.start({});
	}

	void TearDown() override
	{
		if (m_service)
			m_service->stop();
		m_worker.stop();
	}

	ax::BackgroundWorker m_worker{ false, {} };
	std::unique_ptr<ax::NativeTaskService> m_service;
};

TEST_F(NativeTaskTests, TransfersNativeAllocationWithoutLuaGraphCopies)
{
	start({ { "task", R"(
		return function(r, task)
			assert(app.thread == "task" and app.window == nil and app.sprites == nil)
			assert(require == nil and package == nil and app.call_deferred == nil)
			assert(r.input:sealed() and r.input:get(1) == 42)
			local output = bg.number_buffer(2)
			output:set(1, r.input:get(1))
			output:set(2, 1.25)
			return { input = r.input, output = output, flag = true, number = 1.25 }
		end
	)" } });
	auto input{ std::make_shared<ax::NumberBuffer>(1, m_service->budget()) };
	input->set(1, 42);
	input->seal();
	auto task{ m_service->submit(m_worker, "task", { { "input", input } }) };
	ASSERT_TRUE(await_task(task));
	ASSERT_EQ(task->status(), "succeeded") << task->error();
	const auto result{ task->take_result() };
	EXPECT_EQ(std::get<std::shared_ptr<ax::NumberBuffer>>(result.at("input")).get(), input.get());
	const auto output{ std::get<std::shared_ptr<ax::NumberBuffer>>(result.at("output")) };
	EXPECT_TRUE(output->sealed());
	EXPECT_EQ(output->get(1), 42);
	EXPECT_EQ(output->get(2), 1.25);
	EXPECT_DOUBLE_EQ(std::get<double>(result.at("number")), 1.25);
	EXPECT_THROW(output->set(1, 0), sol::error);
	EXPECT_THROW(task->take_result(), sol::error);
	m_service->stop();
	m_worker.stop();
	EXPECT_EQ(output->get(1), 42);
}

TEST_F(NativeTaskTests, NativeBuffersEnforceBoundsOwnershipAndPayloadLimit)
{
	auto budget{ std::make_shared<ax::TaskBufferBudget>() };
	auto buffer{ std::make_shared<ax::NumberBuffer>(2, budget) };
	EXPECT_THROW(buffer->set(0, 1), sol::error);
	EXPECT_THROW(buffer->get(3), sol::error);
	auto ownership{ std::async(std::launch::async, [buffer]()
	{
		try { buffer->set(1, 7); }
		catch (const sol::error&) { return true; }
		return false;
	}) };
	EXPECT_TRUE(ownership.get());
	buffer->seal();
	auto read{ std::async(std::launch::async, [buffer]() { return buffer->get(1); }) };
	EXPECT_EQ(read.get(), 0);
	EXPECT_THROW((void)std::make_shared<ax::NumberBuffer>(ax::TaskBufferBudget::limit / sizeof(double), budget), sol::error);
	EXPECT_EQ(budget->bytes.load(), 2 * sizeof(double));
	buffer.reset();
	EXPECT_EQ(budget->bytes.load(), 0);
}

TEST_F(NativeTaskTests, FlatRecordValidationRejectsGraphsAndUnsealedInputs)
{
	sol::state state;
	state.open_libraries();
	auto budget{ std::make_shared<ax::TaskBufferBudget>() };
	auto bg{ state.create_table() };
	ax::bind_task_buffers(state, bg, budget);
	auto record{ state.create_table() };
	record["graph"] = state.create_table_with("value", 1);
	EXPECT_THROW(ax::task_record_from_lua(record, false), sol::error);
	record["graph"] = sol::nil;
	auto input{ std::make_shared<ax::NumberBuffer>(1, budget) };
	record["input"] = input;
	EXPECT_THROW(ax::task_record_from_lua(record, false), sol::error);
	const auto output{ ax::task_record_from_lua(record, true) };
	EXPECT_TRUE(input->sealed());
	EXPECT_EQ(std::get<std::shared_ptr<ax::NumberBuffer>>(output.at("input")).get(), input.get());
}

TEST_F(NativeTaskTests, CachesTaskModulesAndImportsInWorkerState)
{
	start({
		{ "helper", "return { value = 123 }" },
		{ "task", R"(
			local calls = 0
			local helper = ax.import("helper")
			return function(r, task)
				calls = calls + 1
				return { calls = calls, value = helper.value }
			end
		)" }
	});
	for (std::int64_t i{ 1 }; i <= 3; ++i)
	{
		auto task{ m_service->submit(m_worker, "task", {}) };
		ASSERT_TRUE(await_task(task));
		ASSERT_EQ(task->status(), "succeeded") << task->error();
		const auto result{ task->take_result() };
		EXPECT_EQ(std::get<std::int64_t>(result.at("calls")), i);
		EXPECT_EQ(std::get<std::int64_t>(result.at("value")), 123);
	}
}

TEST_F(NativeTaskTests, ExecutesCompiledProjectBytecode)
{
	sol::state compiler;
	compiler.open_libraries();
	const auto loaded{ compiler.load(text(s_generator, sizeof(s_generator)), "generate_chunk") };
	ASSERT_TRUE(loaded.valid());
	sol::protected_function dump{ compiler["string"]["dump"] };
	const auto bytecode{ dump(loaded.get<sol::protected_function>()).get<std::string>() };
	start({ { "generate_chunk", bytecode } });
	auto task{ m_service->submit(m_worker, "generate_chunk", { { "cx", std::int64_t{ 0 } }, { "cy", std::int64_t{ 0 } } }) };
	ASSERT_TRUE(await_task(task));
	ASSERT_EQ(task->status(), "succeeded") << task->error();
	EXPECT_EQ(std::get<std::shared_ptr<ax::SpriteBuffer>>(task->take_result().at("sprites"))->size(), 1024);
}

TEST_F(NativeTaskTests, CancellationQueueLimitsAndShutdownAreNonBlocking)
{
	start({
		{ "block", "return function(r, task) while not task:cancelled() do end return {} end" },
		{ "task", "return function(r, task) return { value = 1 } end" }
	});
	auto running{ m_service->submit(m_worker, "block", {}) };
	const auto deadline{ std::chrono::steady_clock::now() + std::chrono::seconds(5) };
	while (running->status() != "running" && std::chrono::steady_clock::now() < deadline)
		std::this_thread::yield();
	ASSERT_EQ(running->status(), "running");
	std::vector<std::shared_ptr<ax::NativeTask>> queued;
	for (int i{ 0 }; i < 63; ++i)
		queued.push_back(m_service->submit(m_worker, "task", {}));
	EXPECT_THROW(m_service->submit(m_worker, "task", {}), sol::error);
	m_service->stop();
	m_worker.stop();
	EXPECT_EQ(running->status(), "cancelled");
	for (const auto& task : queued)
		EXPECT_EQ(task->status(), "cancelled");
	EXPECT_EQ(m_service->outstanding(), 0);
	EXPECT_THROW(m_service->submit(m_worker, "task", {}), sol::error);
}

TEST_F(NativeTaskTests, ReportsRuntimeAndMalformedResultFailures)
{
	testlog::LogCapture::instance().expect_total_error_count(2);
	start({
		{ "broken", "return function(r, task) error('expected failure') end" },
		{ "graph", "return function(r, task) return { nested = {} } end" }
	});
	for (const auto& name : { "broken", "graph" })
	{
		auto task{ m_service->submit(m_worker, name, {}) };
		ASSERT_TRUE(await_task(task));
		EXPECT_EQ(task->status(), "failed");
		EXPECT_NE(task->error().find(name), std::string::npos);
		EXPECT_THROW(task->take_result(), sol::error);
	}
	EXPECT_THROW(m_service->submit(m_worker, "missing", {}), sol::error);
}

TEST_F(NativeTaskTests, ConcurrentSubmissionAndDroppedHandlesFinishSafely)
{
	start({ { "task", "return function(r, task) return { value = r.value } end" } });
	std::vector<std::future<std::vector<std::shared_ptr<ax::NativeTask>>>> producers;
	for (int producer{ 0 }; producer < 4; ++producer)
	{
		producers.push_back(std::async(std::launch::async, [this, producer]()
		{
			std::vector<std::shared_ptr<ax::NativeTask>> tasks;
			for (int i{ 0 }; i < 16; ++i)
			{
				tasks.push_back(m_service->submit(m_worker, "task", { { "value", std::int64_t{ producer * 16 + i } } }));
				if (i % 2 == 0)
					tasks.back().reset();
			}
			return tasks;
		}));
	}
	for (auto& producer : producers)
	{
		auto tasks{ producer.get() };
		for (const auto& task : tasks)
		{
			if (!task)
				continue;
			ASSERT_TRUE(await_task(task));
			EXPECT_EQ(task->status(), "succeeded") << task->error();
		}
	}
	m_service->stop();
	m_worker.stop();
	EXPECT_EQ(m_service->outstanding(), 0);
}

TEST_F(NativeTaskTests, StartupFailurePublishesFailedStatus)
{
	testlog::LogCapture::instance().expect_total_error_count(2);
	m_service = std::make_unique<ax::NativeTaskService>(flag_set<ax::lua::Permission>{},
		std::map<std::string, std::string>{ { "task", "return function() return {} end" } });
	m_worker.set_thread_hooks([this]() { m_service->initialize(); }, [this]() { m_service->finalize(); });
	m_worker.start({});
	auto task{ m_service->submit(m_worker, "task", {}) };
	ASSERT_TRUE(await_task(task));
	EXPECT_EQ(task->status(), "failed");
	EXPECT_NE(task->error().find("@std"), std::string::npos);
}
