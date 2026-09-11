#pragma once

#include "lua_bindings.h"
#include "spdlog/spdlog.h"

namespace ax::lua::bindings
{
	void setup_window_bindings(sol::state& env);
}