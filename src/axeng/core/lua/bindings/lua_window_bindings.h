#pragma once

#include "axeng/core/lua/lua_bindings.h"
#include "spdlog/spdlog.h"

namespace ax::lua::bindings
{
	void setup_window_bindings(sol::state&);
}