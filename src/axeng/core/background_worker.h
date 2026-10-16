#pragma once

#include "axeng/core/error.h"
#include "axeng/core/helpers.h"
#include "axeng/core/lua/lua_engine.h"
#include "axeng/core/lua/script.h"

#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>
#include <memory>
#include <vector>
#include <map>

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
		void set_thread_hooks(std::function<void()> start, std::function<void()> stop);

	private:
		std::mutex m_mutex{};
		std::thread m_thread{};
		std::condition_variable m_condition{};
		std::queue<std::shared_ptr<BackgroundTask>> m_queue{};
		std::map<BackgroundTask*, std::shared_ptr<BackgroundTask>> m_tasks{};
		bool m_running{ true };
		std::function<void()> m_onStart;
		std::function<void()> m_onStop;

		// Only to be used on the background thread
		lua::Manager m_lua;
		bool m_useLua{ false };
		bool m_luaInitialized{ true };

		friend struct BackgroundTaskResult;
	};

	struct BackgroundTaskResult
	{
		DISABLE_COPY_AND_MOVE(BackgroundTaskResult);

		explicit BackgroundTaskResult(ax::Error error) : m_error{ error } {}

	public:
		bool is_valid() const { return true; }
		ax::Error get_error() const { return m_error; }
		sol::object get_data() const { return sol::nil; }

	private:
		ax::Error m_error;
	};
}