#include "axeng/core/native_tasks.h"
#include "axeng/core/lua/bindings/lua_log_bindings.h"
#include "axeng/core/lua/bindings/lua_noise_bindings.h"
#include "axeng/core/lua/bindings/lua_vector_bindings.h"

#include <cmath>

namespace
{
	constexpr std::size_t s_max_fields{ 32 };
	constexpr std::size_t s_max_string_bytes{ 4096 };

	void check_finite(double value)
	{
		if (!std::isfinite(value))
			throw sol::error("Task numbers must be finite");
	}
}

std::string ax::NativeTask::status() const
{
	std::lock_guard lock{ m_mutex };
	switch (m_status)
	{
		case Status::Queued: return cancelled() ? "cancelled" : "queued";
		case Status::Running: return "running";
		case Status::Succeeded: return "succeeded";
		case Status::Failed: return "failed";
		case Status::Cancelled: return "cancelled";
	}
	throw std::logic_error("Invalid task status");
}

std::string ax::NativeTask::error() const
{
	std::lock_guard lock{ m_mutex };
	return m_error;
}

ax::TaskRecord ax::NativeTask::take_result()
{
	std::lock_guard lock{ m_mutex };
	if (m_status != Status::Succeeded || m_consumed)
		throw sol::error("Task result is not ready or has already been consumed");
	m_consumed = true;
	return std::move(m_result);
}

ax::TaskRecord ax::task_record_from_lua(const sol::table& table, bool publishing)
{
	TaskRecord record;
	for (const auto& [key, value] : table)
	{
		if (record.size() >= s_max_fields || key.get_type() != sol::type::string)
			throw sol::error("Task records require at most 32 string keys");
		const auto name{ key.as<std::string>() };
		if (name.empty() || name.size() > 128)
			throw sol::error("Invalid task record key");
		switch (value.get_type())
		{
			case sol::type::boolean: record.emplace(name, value.as<bool>()); break;
			case sol::type::number:
				check_finite(value.as<double>());
				if (value.is<std::int64_t>())
					record.emplace(name, value.as<std::int64_t>());
				else
					record.emplace(name, value.as<double>());
				break;
			case sol::type::string:
			{
				auto text{ value.as<std::string>() };
				if (text.size() > s_max_string_bytes)
					throw sol::error("Task string exceeds 4096 bytes");
				record.emplace(name, std::move(text));
				break;
			}
			case sol::type::userdata:
				if (value.is<std::shared_ptr<NumberBuffer>>())
					record.emplace(name, value.as<std::shared_ptr<NumberBuffer>>());
				else if (value.is<std::shared_ptr<SpriteBuffer>>())
					record.emplace(name, value.as<std::shared_ptr<SpriteBuffer>>());
				else
					throw sol::error("Task records only accept native buffer userdata");
				break;
			default:
				throw sol::error("Task records only accept scalars and native buffers, not Lua tables or functions");
		}
	}
	// Validate the complete result before sealing any aliases.
	for (auto& [name, value] : record)
	{
		(void)name;
		std::visit([publishing](auto& entry)
		{
			using T = std::decay_t<decltype(entry)>;
			if constexpr (std::is_same_v<T, std::shared_ptr<NumberBuffer>> || std::is_same_v<T, std::shared_ptr<SpriteBuffer>>)
			{
				if (!entry)
					throw sol::error("Null task buffer");
				if (publishing)
					entry->seal();
				else if (!entry->sealed())
					throw sol::error("Task inputs must contain sealed buffers");
			}
		}, value);
	}
	return record;
}

sol::table ax::task_record_to_lua(sol::state& state, const TaskRecord& record)
{
	auto table{ state.create_table() };
	for (const auto& [name, value] : record)
		std::visit([&](const auto& entry) { table[name] = entry; }, value);
	return table;
}

void ax::bind_task_buffers(sol::state& state, sol::table bg, std::shared_ptr<TaskBufferBudget> budget)
{
	state.new_usertype<NumberBuffer>("number_buffer", sol::no_constructor,
		"size", &NumberBuffer::size, "get", &NumberBuffer::get,
		"set", [](NumberBuffer& buffer, std::size_t index, double value)
		{
			check_finite(value);
			buffer.set(index, value);
		},
		"seal", &NumberBuffer::seal, "sealed", &NumberBuffer::sealed);
	state.new_usertype<SpriteBuffer>("sprite_buffer", sol::no_constructor,
		"size", &SpriteBuffer::size, "seal", &SpriteBuffer::seal, "sealed", &SpriteBuffer::sealed,
		"set", [](SpriteBuffer& buffer, std::size_t index, float x, float y, float z,
			float rx, float ry, float rw, float rh, float sx, float sy, const glm::vec4& tint)
		{
			for (const auto value : { x, y, z, rx, ry, rw, rh, sx, sy, tint.r, tint.g, tint.b, tint.a })
				check_finite(value);
			if (rw <= 0 || rh <= 0)
				throw sol::error("Sprite region size must be positive");
			SpriteGpuData data;
			data.pos = { x, y };
			data.z = z;
			data.region = { rx, ry, rw, rh };
			data.scale = { sx, sy };
			data.tint = tint;
			data.useRegion = 1;
			buffer.set(index, data);
		});
	bg["number_buffer"] = [budget](std::size_t count) { return std::make_shared<NumberBuffer>(count, budget); };
	bg["sprite_buffer"] = [budget](std::size_t count) { return std::make_shared<SpriteBuffer>(count, budget); };
	state.new_usertype<NativeTask>("native_task", sol::no_constructor,
		"status", &NativeTask::status, "cancel", &NativeTask::cancel,
		"cancelled", &NativeTask::cancelled, "error", &NativeTask::error,
		"take_result", [&state](NativeTask& task) { return task_record_to_lua(state, task.take_result()); });
}

void ax::bind_native_tasks(sol::state& state, sol::table bg, NativeTaskService& service, BackgroundWorker& worker)
{
	bind_task_buffers(state, bg, service.budget());
	bg["submit"] = [&service, &worker](const std::string& script, const sol::table& input)
	{
		return service.submit(worker, script, task_record_from_lua(input, false));
	};
	bg["outstanding"] = [&service]() { return service.outstanding(); };
	bg["buffer_bytes"] = [budget = service.budget()]() { return budget->bytes.load(); };
}

ax::NativeTaskService::NativeTaskService(flag_set<lua::Permission> permissions, std::map<std::string, std::string> sources)
	: m_permissions{ permissions }, m_sources{ std::move(sources) }
{
}

ax::NativeTaskService::~NativeTaskService() = default;

void ax::NativeTaskService::initialize()
{
	m_startupError.clear();
	try
	{
		m_runtime = std::make_unique<lua::Manager>(m_permissions);
		if (m_runtime->setup() != Error::Success)
			throw sol::error("Failed to initialize task Lua");
		auto& state{ m_runtime->state() };
		lua::bindings::setup_log_bindings(state);
		lua::bindings::setup_noise_bindings(state);
		lua::bindings::setup_vector_bindings(state);
		auto bg{ state.create_table() };
		state["bg"] = bg;
		bind_task_buffers(state, bg, m_budget);
		state["app"] = state.create_table_with("thread", "task", "on_main_thread", []() { return false; });
		const auto stdSource{ m_sources.find("@std") };
		if (stdSource == m_sources.end())
			throw sol::error("Task runtime requires @std");
		const auto result{ state.safe_script(stdSource->second, sol::script_pass_on_error) };
		if (!result.valid())
			throw sol::error(result);
		state["ax"]["import"] = [this](const std::string& name) { return import(name); };
		// Task code may import registered sources, never dynamically load modules or files.
		state["require"] = sol::nil;
		state["dofile"] = sol::nil;
		state["loadfile"] = sol::nil;
		state["package"] = sol::nil;
	}
	catch (const std::exception& error)
	{
		m_startupError = error.what();
		spdlog::error("Task runtime initialization failed: {}", m_startupError);
	}
}

void ax::NativeTaskService::finalize()
{
	m_modules.clear();
	m_runtime.reset();
}

sol::object ax::NativeTaskService::import(const std::string& script)
{
	if (const auto it{ m_modules.find(script) }; it != m_modules.end())
		return it->second;
	if (!m_importing.insert(script).second)
		throw sol::error("Circular task import: " + script);
	struct ImportScope
	{
		std::set<std::string>& importing;
		const std::string& name;
		~ImportScope() { importing.erase(name); }
	} scope{ m_importing, script };
	const auto source{ m_sources.find(script) };
	if (source == m_sources.end())
		throw sol::error("Unknown task script: " + script);
	auto& state{ m_runtime->state() };
	auto loaded{ state.load(source->second, script) };
	if (!loaded.valid())
		throw sol::error(loaded);
	sol::protected_function function{ loaded };
	auto environment{ m_runtime->create_env() };
	sol::set_environment(environment, function);
	auto result{ function() };
	if (!result.valid())
		throw sol::error(result);
	sol::object module{ result.get<sol::object>() };
	m_modules.emplace(script, module);
	return module;
}

std::shared_ptr<ax::NativeTask> ax::NativeTaskService::submit(BackgroundWorker& worker, const std::string& script, TaskRecord input)
{
	std::lock_guard lock{ m_mutex };
	if (!m_accepting)
		throw sol::error("Task service is stopped");
	if (!m_sources.contains(script))
		throw sol::error("Unknown task script: " + script);
	if (m_outstanding.load() >= 64)
		throw sol::error("Task queue limit (64 outstanding jobs) exceeded");
	auto task{ std::make_shared<NativeTask>() };
	std::erase_if(m_tasks, [](const auto& entry) { return entry.expired(); });
	m_tasks.push_back(task);
	++m_outstanding;
	try
	{
		auto execution{ worker.create() };
		execution->set_task([this, task, script, input = std::move(input)]()
		{
			execute(task, script, input);
			return Error::Success;
		});
		worker.enqueue(execution);
	}
	catch (const std::exception&)
	{
		--m_outstanding;
		throw;
	}
	return task;
}

void ax::NativeTaskService::execute(const std::shared_ptr<NativeTask>& task, const std::string& script, const TaskRecord& input)
{
	TaskRecord output;
	std::string failure;
	{
		std::lock_guard lock{ task->m_mutex };
		task->m_status = NativeTask::Status::Running;
	}
	try
	{
		if (!task->cancelled())
		{
			if (!m_startupError.empty())
				throw sol::error(m_startupError);
			auto module{ import(script) };
			if (module.get_type() != sol::type::function)
				throw sol::error("Task script must return a function");
			sol::protected_function function{ module };
			auto result{ function(task_record_to_lua(m_runtime->state(), input), task) };
			if (!result.valid())
				throw sol::error(result);
			if (!task->cancelled())
			{
				if (result.get_type() != sol::type::table)
					throw sol::error("Task function must return a flat record");
				output = task_record_from_lua(result.get<sol::table>(), true);
			}
		}
	}
	catch (const std::exception& error)
	{
		failure = (script + ": " + error.what()).substr(0, 8192);
		spdlog::error("Background task failed: {}", failure);
	}
	{
		std::lock_guard lock{ task->m_mutex };
		task->m_error = std::move(failure);
		if (task->cancelled())
			task->m_status = NativeTask::Status::Cancelled;
		else if (!task->m_error.empty())
			task->m_status = NativeTask::Status::Failed;
		else
		{
			task->m_result = std::move(output);
			task->m_status = NativeTask::Status::Succeeded;
		}
	}
	if (m_runtime)
		m_runtime->state().collect_garbage();
	--m_outstanding;
}

void ax::NativeTaskService::stop()
{
	std::lock_guard lock{ m_mutex };
	m_accepting = false;
	for (const auto& entry : m_tasks)
		if (const auto task{ entry.lock() })
			task->cancel();
}
