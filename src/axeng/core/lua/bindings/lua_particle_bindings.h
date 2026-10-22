#pragma once

#include "axeng/core/lua/lua_bindings.h"

namespace ax::lua::bindings
{
	void setup_particle_bindings(ax::Application&, sol::state&);
}
