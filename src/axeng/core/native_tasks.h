#pragma once

#include "axeng/core/background_worker.h"
#include "axeng/core/texture.h"

#include <atomic>
#include <variant>
#include <set>

namespace ax
{
	struct TaskBufferBudget
	{
		std::atomic_size_t bytes{ 0 };
		static constexpr std::size_t limit{ 16 * 1024 * 1024 };
	};

	template<typename T>
	class TaskBuffer
	{
	public:
		TaskBuffer(std::size_t count, std::shared_ptr<TaskBufferBudget> budget)
			: m_budget{ std::move(budget) }, m_owner{ std::this_thread::get_id() }
		{
			if (!m_budget)
				throw sol::error("Task buffer requires a payload budget");
			if (count == 0 || count > TaskBufferBudget::limit / sizeof(T))
				throw sol::error("Invalid task buffer size");
			m_bytes = count * sizeof(T);
			if (m_budget->bytes.fetch_add(m_bytes) > TaskBufferBudget::limit - m_bytes)
			{
				m_budget->bytes.fetch_sub(m_bytes);
				throw sol::error("Task buffer payload limit (16 MiB) exceeded");
			}
			try
			{
				m_data.resize(count);
			}
			catch (const std::bad_alloc&)
			{
				m_budget->bytes.fetch_sub(m_bytes);
				throw;
			}
		}
		~TaskBuffer() { m_budget->bytes.fetch_sub(m_bytes); }
		DISABLE_COPY_AND_MOVE(TaskBuffer);

		std::size_t size() const { return m_data.size(); }
		bool sealed() const { return m_sealed.load(); }
		void seal()
		{
			if (!sealed() && m_owner != std::this_thread::get_id())
				throw sol::error("Only the buffer owner may seal it");
			m_sealed.store(true);
		}
		T get(std::size_t index) const
		{
			check_read();
			return m_data.at(offset(index));
		}
		void set(std::size_t index, T value)
		{
			if (sealed() || m_owner != std::this_thread::get_id())
				throw sol::error("Task buffer is immutable or belongs to another thread");
			m_data.at(offset(index)) = std::move(value);
		}
		const std::vector<T>& data() const
		{
			check_read();
			return m_data;
		}
	private:
		std::size_t offset(std::size_t index) const
		{
			if (index == 0 || index > size())
				throw sol::error("Task buffer index is out of bounds");
			return index - 1;
		}
		void check_read() const
		{
			if (!sealed() && m_owner != std::this_thread::get_id())
				throw sol::error("Unsealed task buffer belongs to another thread");
		}
		std::shared_ptr<TaskBufferBudget> m_budget;
		std::thread::id m_owner;
		std::vector<T> m_data;
		std::size_t m_bytes{};
		std::atomic_bool m_sealed{ false };
	};

	using NumberBuffer = TaskBuffer<double>;
	using SpriteBuffer = TaskBuffer<SpriteGpuData>;
	using TaskValue = std::variant<bool, std::int64_t, double, std::string,
		std::shared_ptr<NumberBuffer>, std::shared_ptr<SpriteBuffer>>;
	using TaskRecord = std::map<std::string, TaskValue>;

	class NativeTask
	{
	public:
		enum class Status { Queued, Running, Succeeded, Failed, Cancelled };
		std::string status() const;
		void cancel() { m_cancelled.store(true); }
		bool cancelled() const { return m_cancelled.load(); }
		std::string error() const;
		TaskRecord take_result();
	private:
		mutable std::mutex m_mutex;
		Status m_status{ Status::Queued };
		std::atomic_bool m_cancelled{ false };
		bool m_consumed{ false };
		std::string m_error;
		TaskRecord m_result;
		friend class NativeTaskService;
	};

	class NativeTaskService
	{
	public:
		NativeTaskService(flag_set<lua::Permission> permissions, std::map<std::string, std::string> sources);
		~NativeTaskService();
		void initialize();
		void finalize();
		void stop();
		std::shared_ptr<NativeTask> submit(BackgroundWorker& worker, const std::string& script, TaskRecord input);
		std::size_t outstanding() const { return m_outstanding.load(); }
		std::shared_ptr<TaskBufferBudget> budget() const { return m_budget; }
	private:
		void execute(const std::shared_ptr<NativeTask>& task, const std::string& script, const TaskRecord& input);
		sol::object import(const std::string& script);
		flag_set<lua::Permission> m_permissions;
		std::map<std::string, std::string> m_sources;
		std::shared_ptr<TaskBufferBudget> m_budget{ std::make_shared<TaskBufferBudget>() };
		std::unique_ptr<lua::Manager> m_runtime;
		std::map<std::string, sol::object> m_modules;
		std::set<std::string> m_importing;
		std::string m_startupError;
		std::mutex m_mutex;
		std::vector<std::weak_ptr<NativeTask>> m_tasks;
		std::atomic_size_t m_outstanding{ 0 };
		bool m_accepting{ true };
	};

	TaskRecord task_record_from_lua(const sol::table& table, bool publishing);
	sol::table task_record_to_lua(sol::state& state, const TaskRecord& record);
	void bind_task_buffers(sol::state& state, sol::table bg, std::shared_ptr<TaskBufferBudget> budget);
	void bind_native_tasks(sol::state& state, sol::table bg, NativeTaskService& service, BackgroundWorker& worker);
}
