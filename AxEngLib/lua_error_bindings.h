#pragma once

#include "lua_bindings.h"

namespace ax::lua::bindings
{
	void setup_error_bindings(sol::state&);
}