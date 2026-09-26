#pragma once

#include "lua_bindings.h"

namespace ax::lua::bindings
{
	void setup_mouse_bindings(sol::state&);
	void cleanup_mouse_bindings(sol::state&);
}