#include "log_timer.h"

ax::LogTimer::LogTimer(std::string_view message)
	: m_message{ message },
	m_timer{}
{
}

ax::LogTimer::~LogTimer()
{
	spdlog::info("{}: {}s", m_message, m_timer);
}
