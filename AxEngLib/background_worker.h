#pragma once

#include "error.h"
#include "helpers.h"
#include "lua_engine.h"
#include "script.h"

#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>
#include <memory>
#include <vector>

namespace ax
{
	struct BackgroundTaskResult;
	class BackgroundWorker;

	class BackgroundTask
	{
	public:
		BackgroundTask() = default;
		DISABLE_COPY_AND_MOVE(BackgroundTask);

		using task_t = std::function<ax::Error()>;
		using success_t = std::function<void(std::unique_ptr<BackgroundTaskResult>)>;
		using failure_t = std::function<void(std::unique_ptr<BackgroundTaskResult>)>;

		BackgroundTask& set_task(task_t t) { task = std::move(t); return *this; }
		BackgroundTask& set_success(success_t cb) { successCallback = std::move(cb); return *this; }
		BackgroundTask& set_failure(failure_t cb) { failureCallback = std::move(cb); return *this; }

	private:
		ax::Error error{ ax::Error::Success };
		sol::object data{};
		std::condition_variable finished{};
		std::mutex finishedMutex{};

		std::function<ax::Error()> task;
		success_t successCallback;
		failure_t failureCallback;

		friend struct BackgroundTaskResult;
		friend class BackgroundWorker;
	};

	class BackgroundWorker
	{
	public:
		DISABLE_COPY_AND_MOVE(BackgroundWorker);
		
		BackgroundWorker(bool useLua, flag_set<lua::Permission> permissions);
		~BackgroundWorker();

		void start(const std::vector<std::string>& args);
		void stop();
		BackgroundTask* create();
		void enqueue(BackgroundTask*);

		BackgroundTask* create_script_task(lua::Script*);

		lua::Manager& lua() { return m_lua; }

	private:
		std::mutex m_mutex{};
		std::thread m_thread{};
		std::condition_variable m_condition{};
		std::queue<BackgroundTask*> m_queue{};
		std::vector<std::unique_ptr<BackgroundTask>> m_tasks{};
		bool m_running{ true };

		// Only to be used on the background thread
		lua::Manager m_lua;
		bool m_useLua{ false };

		friend struct BackgroundTaskResult;
	};

	struct BackgroundTaskResult
	{
		DISABLE_COPY_AND_MOVE(BackgroundTaskResult);

		BackgroundTaskResult(BackgroundWorker& w, BackgroundTask* t) : worker(w), task(t) {}
		~BackgroundTaskResult()
		{
			if (task)
				std::erase_if(worker.m_tasks, [this](const auto& ownedTask) { return ownedTask.get() == task; });
		}

	public:
		bool is_valid() const { return task; }
		ax::Error get_error() const { return task ? task->error : ax::Error::GenericFailure; }
		sol::object get_data() const { return task ? task->data : sol::nil; }

	private:
		BackgroundTask* task;
		BackgroundWorker& worker;
	};
}