#pragma once

#include "lua_bindings.h"
#include "custom_type.h"

namespace ax::lua::bindings
{
	void setup_type_bindings(sol::state&);
}