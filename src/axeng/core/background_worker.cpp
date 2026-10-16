#include "axeng/core/background_worker.h"
#include "axeng/core/lua/shared.h"

ax::BackgroundWorker::BackgroundWorker(bool useLua, flag_set<lua::Permission> permissions)
	: m_lua{ permissions }
	, m_useLua{ useLua }
{
}

ax::BackgroundWorker::~BackgroundWorker()
{
	stop();
}

void ax::BackgroundWorker::start(const std::vector<std::string>& args)
{
	{
		std::lock_guard lock{ m_mutex };
		if (m_thread.joinable())
			throw std::logic_error("Background worker is already started");
		m_running = true;
	}

	m_thread = std::thread([this, args]()
	{
		try
		{
			if (m_onStart)
				m_onStart();
		}
		catch (const std::exception& error)
		{
			spdlog::error("Background worker start hook raised an exception: {}", error.what());
		}
		
		m_luaInitialized = true;
		if (m_useLua)
		{
			m_lua.state()["args"] = args;

			if (sol::optional<sol::table> app{ m_lua.state()["app"] })
			{
				(*app)["run_in_this_environment"] =
					[this](const sol::table& script) -> sol::object
					{
						if (script["valid"])
						{
							auto ptr{ script["ptr"].get<ax::lua::Script*>() };
							if (ptr == nullptr)
							{
								spdlog::error("Invalid script object, cannot run");
								return nullptr;
							}
							auto env{ m_lua.create_env() };
							auto res{ ptr->run_different_state(m_lua.state(), env) };

							if (!res.valid())
							{
								const sol::error msg = res;
								spdlog::error("Failed to run script: {}", msg.what());
							}
							return res;
						}
						else
						{
							spdlog::error("Invalid script, cannot run");
							return nullptr;
						}
					};
			}

			const auto stdLibText{ Resource::embedded_load_as_text<lua::Script>("@std") };
			if (!stdLibText)
			{
				spdlog::error("Failed to load background @std script");
				m_luaInitialized = false;
			}
			else
			{
				const auto res{ m_lua.state().do_string(*stdLibText, "@std") };
				if (!res.valid())
				{
					sol::error err = res;
					spdlog::error("Failed to run @std script {}", err.what());
					m_luaInitialized = false;
				}
			}
		}
		for (;;)
		{
			std::shared_ptr<BackgroundTask> task;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_condition.wait(lock, [this]() { return !m_queue.empty() || !m_running; });

				if (!m_running && m_queue.empty())
					break;

				task = m_queue.front();
				m_queue.pop();
			}

			if (task == nullptr)
			{
				spdlog::error("Null task propagated?");
				continue;
			}

			ax::Error err{ ax::Error::Success };
			try
			{
				err = task->task();
			}
			catch (const std::exception& error)
			{
				spdlog::error("Background task raised an exception: {}", error.what());
				err = ax::Error::GenericFailure;
			}
			auto result{ std::make_unique<BackgroundTaskResult>(err) };
			try
			{
				if (err == ax::Error::Success)
				{
					if (task->successCallback)
						task->successCallback(std::move(result));
				}
				else
				{
					task->error = err;
					if (task->failureCallback)
						task->failureCallback(std::move(result));
				}
			}
			catch (const std::exception& error)
			{
				spdlog::error("Background task callback raised an exception: {}", error.what());
			}

		}
		
		try
		{
			if (m_onStop)
				m_onStop();
		}
		catch (const std::exception& error)
		{
			spdlog::error("Background worker stop hook raised an exception: {}", error.what());
		}
	});
}

void ax::BackgroundWorker::stop()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_running = false;
	}
	m_condition.notify_one();

	if (m_thread.joinable())
		m_thread.join();
}

ax::BackgroundTask* ax::BackgroundWorker::create()
{
	std::lock_guard lock{ m_mutex };
	if (!m_running)
		throw std::logic_error("Background worker is stopped");
	auto task{ std::make_shared<BackgroundTask>() };
	const auto ptr{ task.get() };
	m_tasks.emplace(ptr, std::move(task));
	return ptr;
}

void ax::BackgroundWorker::enqueue(BackgroundTask* task)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_running)
		throw std::logic_error("Background worker is stopped");
	const auto it{ m_tasks.find(task) };
	if (it == m_tasks.end())
		throw std::invalid_argument("Task is invalid or already enqueued");
	m_queue.push(std::move(it->second));
	m_tasks.erase(it);
	m_condition.notify_one();
}

void ax::BackgroundWorker::set_thread_hooks(std::function<void()> start, std::function<void()> stop)
{
	if (m_thread.joinable())
		throw std::logic_error("Set worker hooks before starting");
	m_onStart = std::move(start);
	m_onStop = std::move(stop);
}

ax::BackgroundTask* ax::BackgroundWorker::create_script_task(lua::Script* script)
{
	if (!m_useLua)
		return nullptr;

	auto task{ create() };
	task->set_task
	(
		[this, script](void)
		{
			if (!m_luaInitialized)
				return ax::Error::Lua;
			auto env{ m_lua.create_env() };
			auto res{ script->run_different_state(m_lua.state(), env) };

			if (!res.valid())
			{
				const sol::error msg = res;
				spdlog::error("Failed to run script: {}", msg.what());
				return ax::Error::Lua;
			}

			return ax::Error::Success;
		}
	);
	return task;
}
