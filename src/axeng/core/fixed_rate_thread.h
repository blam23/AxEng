#pragma once

#include "axeng/core/fixed_update_scheduler.h"
#include "axeng/core/helpers.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

namespace ax
{
	// Runs a callback at a fixed rate on its own thread, with the same bounded catch-up as the window's fixed
	// updates. In `Requested` mode it runs exactly the ticks another thread asks for instead, so a caller can
	// keep it in lock step with its own updates.
	class FixedRateThread
	{
	public:
		enum class Mode { Free, Requested };
		using Clock = std::function<double()>;

		// `now` returns seconds from any fixed origin; it defaults to the steady clock and lets tests control time.
		explicit FixedRateThread(std::function<void()> tick, double rate = 60.0, std::uint32_t maxSteps = 6, Clock now = {});
		~FixedRateThread();
		DISABLE_COPY_AND_MOVE(FixedRateThread);

		void start(Mode mode);
		// Stops after the tick in progress and joins the thread. Rethrows an exception a tick threw.
		void stop();
		bool running() const noexcept { return m_thread.joinable(); }
		Mode mode() const noexcept { return m_mode.load(std::memory_order_acquire); }
		// Switching mode keeps the thread; free running restarts its timing.
		void set_mode(Mode mode);

		// Free mode: no ticks run while paused, and the time spent paused is not caught up.
		void set_paused(bool paused);
		// Requested mode: runs `ticks` more ticks as soon as possible.
		void request(std::uint32_t ticks = 1);
		// Blocks until every requested tick has run (or the thread stopped). Rethrows an exception a tick threw.
		void wait_requested();
		// Wakes the thread to check its clock again (after the clock a test controls has moved).
		void poke();

		std::uint64_t ticks() const noexcept { return m_ticks.load(std::memory_order_acquire); }
		double discarded_seconds() const;
		// Rethrows, once, an exception a tick threw; the thread stops after a failed tick.
		void rethrow_failure();

	private:
		void run();
		double now() const;

		std::function<void()> m_tick;
		Clock m_now;
		FixedUpdateScheduler m_scheduler;
		mutable std::mutex m_mutex{};
		std::condition_variable m_changed{};
		std::thread m_thread{};
		std::atomic<Mode> m_mode{ Mode::Free };
		std::atomic<std::uint64_t> m_ticks{};
		std::uint64_t m_requested{};
		double m_previous{};
		bool m_paused{}, m_stopping{}, m_restartTiming{};
		std::exception_ptr m_failure{};
	};
}
