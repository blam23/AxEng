#pragma once

#include "axeng/core/lua/lua_bindings.h"

namespace ax::lua::bindings
{
	void setup_user_io_bindings(ax::Application&, sol::state&);
}
