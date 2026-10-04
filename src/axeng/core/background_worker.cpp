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
	m_running = true;

	m_thread = std::thread([this, args]()
	{
		if (m_useLua)
		{
			m_lua.state()["args"] = args;

			m_lua.state().new_usertype<lua::SharedObject>
				(
					"shared",
					"set", &lua::SharedObject::set,
					"get", [this](lua::SharedObject& ref, const std::string& key) { return ref.get(m_lua.state(), key); }
				);

			sol::table app{ m_lua.state()["app"].get<sol::table>() };
			app["run_in_this_environment"] =
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

			app["thread"] = "background";
			app["on_main_thread"] =
				[]() -> bool
				{
					return false;
				};

			app["signal"] =
				[](lua::SharedObject& obj, const std::string& signal_name)
				{
					obj.signal(signal_name);
				};

			app["on_signal"] =
				[](lua::SharedObject& obj, const std::string& signal_name, sol::protected_function func)
				{
					obj.on_signal(nullptr, signal_name, func);
				};

			const auto stdLibText{ Resource::embedded_load_as_text<lua::Script>("@std") };
			const auto res{ m_lua.state().do_string(*stdLibText, "@std") };
			if (!res.valid())
			{
				sol::error err = res;
				spdlog::error("Failed to run @std script {}", err.what());
				return;
			}
		}

		for (;;)
		{
			BackgroundTask* task;
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

			auto result = std::make_unique<BackgroundTaskResult>(*this, task);

			ax::Error err = task->task();
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

			{
				std::lock_guard<std::mutex> lock(task->finishedMutex);
				task->finished.notify_one();
			}
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
	m_tasks.emplace_back(std::make_unique<BackgroundTask>());
	return m_tasks.back().get();
}

void ax::BackgroundWorker::enqueue(BackgroundTask* task)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_queue.push(task);
	m_condition.notify_one();
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
