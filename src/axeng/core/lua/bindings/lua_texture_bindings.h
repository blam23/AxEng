#pragma once

#include "axeng/core/lua/lua_bindings.h"
#include "axeng/core/texture.h"

namespace ax::lua::bindings
{
	void setup_texture_bindings(sol::state&);
	rectf get_texture_region(const sol::table& texture);
	rectf get_texture_region(const sol::table& texture, rectf localRegion);
}