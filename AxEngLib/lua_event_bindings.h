#pragma once

#include "lua_bindings.h"

namespace ax::lua::bindings
{
	void setup_event_bindings(sol::state&);
}