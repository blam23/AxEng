#include "axeng/core/fixed_rate_thread.h"

#include <chrono>
#include <tracy/Tracy.hpp>

namespace ax
{
	FixedRateThread::FixedRateThread(std::function<void()> tick, double rate, std::uint32_t maxSteps, Clock now)
		: m_tick{ std::move(tick) }, m_now{ std::move(now) }, m_scheduler{ rate, maxSteps }
	{
	}

	FixedRateThread::~FixedRateThread()
	{
		try
		{
			stop();
		}
		catch (...)
		{
		}
	}

	double FixedRateThread::now() const
	{
		if (m_now) return m_now();
		return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	void FixedRateThread::start(Mode mode)
	{
		stop();
		{
			std::lock_guard lock{ m_mutex };
			m_stopping = false;
			m_restartTiming = true;
			m_requested = m_ticks.load();
			m_failure = nullptr;
		}
		m_mode.store(mode, std::memory_order_release);
		m_thread = std::thread{ [this] { run(); } };
	}

	void FixedRateThread::stop()
	{
		if (m_thread.joinable())
		{
			{
				std::lock_guard lock{ m_mutex };
				m_stopping = true;
			}
			m_changed.notify_all();
			m_thread.join();
		}
		rethrow_failure();
	}

	void FixedRateThread::set_mode(Mode mode)
	{
		{
			std::lock_guard lock{ m_mutex };
			m_mode.store(mode, std::memory_order_release);
			m_restartTiming = true;
			m_requested = m_ticks.load();
		}
		m_changed.notify_all();
	}

	void FixedRateThread::set_paused(bool paused)
	{
		{
			std::lock_guard lock{ m_mutex };
			if (m_paused == paused) return;
			m_paused = paused;
			m_restartTiming = true;
		}
		m_changed.notify_all();
	}

	void FixedRateThread::request(std::uint32_t ticks)
	{
		{
			std::lock_guard lock{ m_mutex };
			m_requested += ticks;
		}
		m_changed.notify_all();
	}

	void FixedRateThread::wait_requested()
	{
		{
			std::unique_lock lock{ m_mutex };
			m_changed.wait(lock, [this]
			{
				return m_stopping || m_failure || !m_thread.joinable() || m_ticks.load() >= m_requested
					|| m_mode.load() != Mode::Requested;
			});
		}
		rethrow_failure();
	}

	void FixedRateThread::poke()
	{
		{
			std::lock_guard lock{ m_mutex };
		}
		m_changed.notify_all();
	}

	double FixedRateThread::discarded_seconds() const
	{
		std::lock_guard lock{ m_mutex };
		return m_scheduler.discarded_seconds();
	}

	void FixedRateThread::rethrow_failure()
	{
		std::exception_ptr failure{};
		{
			std::lock_guard lock{ m_mutex };
			failure = std::exchange(m_failure, nullptr);
		}
		if (failure) std::rethrow_exception(failure);
	}

	void FixedRateThread::run()
	{
#ifdef TRACY_ENABLE
		tracy::SetThreadName("Fixed rate thread");
#endif
		std::unique_lock lock{ m_mutex };
		while (!m_stopping)
		{
			std::uint32_t due{};
			if (m_mode.load() == Mode::Requested)
			{
				if (m_ticks.load() >= m_requested)
				{
					m_changed.wait(lock, [this] { return m_stopping || m_ticks.load() < m_requested || m_mode.load() != Mode::Requested; });
					continue;
				}
				due = 1;
			}
			else
			{
				const auto current{ now() };
				if (m_restartTiming || m_paused)
				{
					m_restartTiming = false;
					m_scheduler.reset_timing();
					m_previous = current;
				}
				if (m_paused)
				{
					m_changed.wait(lock, [this] { return m_stopping || !m_paused || m_mode.load() != Mode::Free; });
					continue;
				}
				due = m_scheduler.advance(current - m_previous);
				m_previous = current;
				if (due == 0)
				{
					const auto wait{ m_scheduler.wait_seconds() };
					m_changed.wait_for(lock, std::chrono::duration<double>(wait));
					continue;
				}
			}
			lock.unlock();
			try
			{
				for (std::uint32_t step{}; step < due; ++step)
				{
					ZoneScopedN("Fixed rate tick");
					m_tick();
					m_ticks.fetch_add(1, std::memory_order_acq_rel);
				}
			}
			catch (...)
			{
				lock.lock();
				m_failure = std::current_exception();
				m_stopping = true;
				lock.unlock();
				m_changed.notify_all();
				lock.lock();
				break;
			}
			lock.lock();
			m_changed.notify_all();
		}
	}
}
