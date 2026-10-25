#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ax
{
	// Elapsed time is supplied by the host so scheduling can also be tested without sleeping.
	class FixedUpdateScheduler
	{
	public:
		explicit FixedUpdateScheduler(double rate = 60.0, std::uint32_t maxSteps = 6)
			: m_delta{ 1.0 / rate }, m_maxSteps{ maxSteps } {}

		std::uint32_t advance(double elapsed) noexcept
		{
			if (!std::isfinite(elapsed) || elapsed < 0.0) return 0;
			m_accumulator += elapsed;
			const double due{ std::floor((m_accumulator + m_delta * 1e-9) / m_delta) };
			const auto steps{ static_cast<std::uint32_t>(std::min(due, static_cast<double>(m_maxSteps))) };
			if (due > m_maxSteps) m_discardedSeconds += (due - m_maxSteps) * m_delta;
			m_accumulator = std::max(0.0, m_accumulator - due * m_delta);
			return steps;
		}

		void complete_update() noexcept { ++m_updates; }
		bool render_due() const noexcept { return m_updates > m_renderedUpdate; }
		void complete_render() noexcept { m_renderedUpdate = m_updates; }
		void reset_timing() noexcept { m_accumulator = 0.0; }
		double delta() const noexcept { return m_delta; }
		double wait_seconds() const noexcept { return std::max(0.0, m_delta - m_accumulator); }
		double discarded_seconds() const noexcept { return m_discardedSeconds; }
		std::uint64_t updates() const noexcept { return m_updates; }

	private:
		double m_delta;
		std::uint32_t m_maxSteps;
		double m_accumulator{};
		double m_discardedSeconds{};
		std::uint64_t m_updates{};
		std::uint64_t m_renderedUpdate{};
	};
}
