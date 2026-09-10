#include "log_timer.h"

static bool s_enable_timers{ false };

ax::LogTimer::LogTimer(std::string_view message)
	: m_message{ message },
	m_timer{}
{
}

ax::LogTimer::~LogTimer()
{
	if (s_enable_timers)
		spdlog::info("<Timer> {}: {}ms", m_message, std::chrono::duration_cast<std::chrono::nanoseconds>(m_timer.elapsed()).count() / 1000000.0);
}

void ax::enable_log_timers()
{
	s_enable_timers = true;
}
