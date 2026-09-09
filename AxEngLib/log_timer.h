#pragma once

#include <string_view>
#include "spdlog/spdlog.h"
#include "spdlog/stopwatch.h"

namespace ax
{
	class LogTimer
	{
	public:
		LogTimer(std::string_view message);
		~LogTimer();

	private:
		std::string_view m_message;
		spdlog::stopwatch m_timer;
	};
}