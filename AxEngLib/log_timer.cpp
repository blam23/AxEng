#include "log_timer.h"

ax::LogTimer::LogTimer(std::string_view message)
	: m_message{ message },
	m_timer{}
{
}

ax::LogTimer::~LogTimer()
{
	spdlog::debug("{}: {}ms", m_message, std::chrono::duration_cast<std::chrono::nanoseconds>(m_timer.elapsed()).count() / 1000000.0);
}
