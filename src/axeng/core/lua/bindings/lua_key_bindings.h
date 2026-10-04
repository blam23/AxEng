#pragma once

#include "axeng/core/lua/lua_bindings.h"

namespace ax::lua::bindings
{
	void setup_key_bindings(sol::state&);
	void cleanup_key_bindings(sol::state&);
}