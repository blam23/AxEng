#pragma once

#include "axeng/core/lua/lua_bindings.h"
#include "axeng/core/custom_type.h"

namespace ax::lua::bindings
{
	void setup_type_bindings(sol::state&);
}