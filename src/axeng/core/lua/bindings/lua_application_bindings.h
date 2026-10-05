#pragma once

#include "axeng/core/lua/lua_bindings.h"
#include "axeng/core/forward.h"

namespace ax::lua::bindings
{
	void setup_application_bindings(ax::Application&, sol::state&, bool mainThread);
}
