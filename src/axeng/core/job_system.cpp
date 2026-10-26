#include "axeng/core/job_system.h"

#include <algorithm>
#include <format>
#include <string>
#include <tracy/Tracy.hpp>

namespace ax
{
	JobSystem::JobSystem(std::size_t workers)
	{
		m_threads.reserve(workers);
		for (std::size_t index{}; index < workers; ++index)
			m_threads.emplace_back([this, index] { worker(index); });
	}

	JobSystem::~JobSystem()
	{
		{
			std::lock_guard lock{ m_mutex };
			m_stopping = true;
		}
		m_changed.notify_all();
		for (auto& thread : m_threads) thread.join();
	}

	std::size_t JobSystem::default_worker_count() noexcept
	{
		const auto hardware{ static_cast<std::size_t>(std::thread::hardware_concurrency()) };
		return hardware > 2 ? hardware - 2 : 0;
	}

	JobSystem::Group::~Group()
	{
		try
		{
			wait();
		}
		catch (...)
		{
		}
	}

	void JobSystem::Group::run(std::function<void()> task)
	{
		m_pending.fetch_add(1, std::memory_order_relaxed);
		if (m_jobs.m_threads.empty())
		{
			Task inline_task{ std::move(task), this };
			m_jobs.execute(inline_task);
			return;
		}
		m_jobs.push({ std::move(task), this });
	}

	void JobSystem::Group::wait()
	{
		while (m_pending.load(std::memory_order_acquire) != 0)
		{
			if (m_jobs.run_one()) continue;
			std::unique_lock lock{ m_jobs.m_mutex };
			m_jobs.m_changed.wait(lock, [this]
			{
				return m_pending.load(std::memory_order_acquire) == 0 || !m_jobs.m_queue.empty();
			});
		}
		std::exception_ptr error{};
		{
			std::lock_guard lock{ m_errorMutex };
			error = std::exchange(m_error, nullptr);
		}
		if (error) std::rethrow_exception(error);
	}

	void JobSystem::Group::fail(std::exception_ptr error) noexcept
	{
		std::lock_guard lock{ m_errorMutex };
		if (!m_error) m_error = std::move(error);
	}

	void JobSystem::parallel_for(std::size_t count, std::size_t grain,
		const std::function<void(std::size_t, std::size_t)>& body)
	{
		if (count == 0) return;
		grain = std::max<std::size_t>(grain, 1);
		const auto chunks{ (count + grain - 1) / grain };
		const auto chunk_range{ [count, grain](std::size_t chunk)
		{
			const auto begin{ chunk * grain };
			return std::pair{ begin, std::min(count, begin + grain) };
		} };
		if (chunks == 1 || m_threads.empty())
		{
			for (std::size_t chunk{}; chunk < chunks; ++chunk)
			{
				const auto [begin, end] { chunk_range(chunk) };
				body(begin, end);
			}
			return;
		}
		std::atomic<std::size_t> next{};
		const auto drain{ [&next, &body, &chunk_range, chunks]
		{
			for (auto chunk{ next.fetch_add(1, std::memory_order_relaxed) }; chunk < chunks;
				chunk = next.fetch_add(1, std::memory_order_relaxed))
			{
				const auto [begin, end] { chunk_range(chunk) };
				body(begin, end);
			}
		} };
		Group group{ *this };
		const auto helpers{ std::min(m_threads.size(), chunks - 1) };
		for (std::size_t helper{}; helper < helpers; ++helper) group.run(drain);
		try
		{
			drain();
		}
		catch (...)
		{
			// Let the helpers stop early; the chunks they already took still finish.
			next.store(chunks, std::memory_order_relaxed);
			group.fail(std::current_exception());
		}
		group.wait();
	}

	void JobSystem::push(Task task)
	{
		{
			std::lock_guard lock{ m_mutex };
			m_queue.push_back(std::move(task));
		}
		m_changed.notify_all();
	}

	bool JobSystem::run_one()
	{
		Task task{};
		{
			std::lock_guard lock{ m_mutex };
			if (m_queue.empty()) return false;
			task = std::move(m_queue.front());
			m_queue.pop_front();
		}
		execute(task);
		return true;
	}

	void JobSystem::execute(Task& task) noexcept
	{
		try
		{
			task.work();
		}
		catch (...)
		{
			task.group->fail(std::current_exception());
		}
		m_tasksRun.fetch_add(1, std::memory_order_relaxed);
		// The last task of a group wakes the threads waiting for it. Taking the lock orders the wake after the
		// waiter's check, so the wake cannot be missed.
		if (task.group->m_pending.fetch_sub(1, std::memory_order_acq_rel) == 1)
		{
			{
				std::lock_guard lock{ m_mutex };
			}
			m_changed.notify_all();
		}
	}

	void JobSystem::worker(std::size_t index)
	{
#ifdef TRACY_ENABLE
		const auto name{ std::format("Job worker {}", index + 1) };
		tracy::SetThreadName(name.c_str());
#else
		(void)index;
#endif
		while (true)
		{
			Task task{};
			{
				std::unique_lock lock{ m_mutex };
				m_changed.wait(lock, [this] { return m_stopping || !m_queue.empty(); });
				if (m_queue.empty()) return;
				task = std::move(m_queue.front());
				m_queue.pop_front();
			}
			execute(task);
		}
	}
}
