#pragma once

#include "lua_bindings.h"
#include "spdlog/spdlog.h"

namespace ax::lua::bindings
{
	void setup_log_bindings(sol::state& env);
}