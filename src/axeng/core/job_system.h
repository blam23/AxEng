#pragma once

#include "axeng/core/helpers.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace ax
{
	// A fixed pool of worker threads for fork-join work. A thread waiting for work it forked runs queued tasks
	// meanwhile, so tasks may fork and wait for work of their own. Chunked loops split their index range the same
	// way for any number of workers, so a result that depends on chunk boundaries never depends on the machine.
	class JobSystem
	{
	public:
		// `workers` threads in addition to the threads that fork work. With none, work runs on the forking thread.
		explicit JobSystem(std::size_t workers = default_worker_count());
		~JobSystem();
		DISABLE_COPY_AND_MOVE(JobSystem);

		// The hardware threads less two: one for the thread that forks the work and one for the rest of the
		// application.
		static std::size_t default_worker_count() noexcept;
		std::size_t worker_count() const noexcept { return m_threads.size(); }

		// Tasks forked together. `wait` returns when every task of the group has finished and rethrows the first
		// exception a task threw. Destroying a group waits for it, discarding exceptions.
		class Group
		{
		public:
			explicit Group(JobSystem& jobs) noexcept : m_jobs{ jobs } {}
			~Group();
			DISABLE_COPY_AND_MOVE(Group);

			void run(std::function<void()> task);
			void wait();

		private:
			friend class JobSystem;
			void fail(std::exception_ptr error) noexcept;
			JobSystem& m_jobs;
			std::atomic<std::size_t> m_pending{};
			std::mutex m_errorMutex{};
			std::exception_ptr m_error{};
		};

		// Calls body(begin, end) for each chunk of [0, count): consecutive ranges of `grain` indices (the last may be
		// shorter). Chunks run in parallel and in no particular order. Returns when all have run; rethrows the first
		// exception.
		void parallel_for(std::size_t count, std::size_t grain,
			const std::function<void(std::size_t begin, std::size_t end)>& body);

		// Total tasks run by the workers and by waiting threads, for tests and profiling.
		std::size_t tasks_run() const noexcept { return m_tasksRun.load(std::memory_order_relaxed); }

	private:
		struct Task
		{
			std::function<void()> work{};
			Group* group{};
		};

		void push(Task task);
		// Runs one queued task; false when the queue is empty.
		bool run_one();
		void execute(Task& task) noexcept;
		void worker(std::size_t index);

		std::mutex m_mutex{};
		std::condition_variable m_changed{};
		std::deque<Task> m_queue{};
		std::vector<std::thread> m_threads{};
		std::atomic<std::size_t> m_tasksRun{};
		bool m_stopping{};
	};
}
