#include "scripting.h"

// Logging
#include "spdlog/spdlog.h"
#include "spdlog/stopwatch.h"

ax::Script::Script()
{
	m_state.open_libraries
	(
		sol::lib::base,
		sol::lib::package,
		sol::lib::math,
		sol::lib::string,
		sol::lib::table,
		sol::lib::bit32
	);
}
