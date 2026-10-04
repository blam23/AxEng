#pragma once

#include "axeng/core/lua/lua_bindings.h"

namespace ax::lua::bindings
{
	void setup_imgui_bindings(sol::state&);
	void cleanup_imgui_bindings(sol::state&);
}