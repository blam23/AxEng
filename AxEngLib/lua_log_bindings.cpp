#include "lua_log_bindings.h"

void ax::lua::bindings::setup_log_bindings(sol::state& state)
{
	auto log_table = state.create_table();

	log_table["info"] = 
		[](const std::string& message)
		{
			spdlog::info(message);
		};

	log_table["warn"] = 
		[](const std::string& message)
		{
			spdlog::warn(message);
		};

	log_table["error"] =
		[](const std::string& message)
		{
			spdlog::error(message);
		};

	state["log"] = log_table;
}
