#pragma once

#include "axeng/core/lua/lua_bindings.h"
#include "axeng/core/texture.h"

namespace ax::lua::bindings
{
	struct LuaTextureLease
	{
		std::shared_ptr<Texture> texture;
	};

	void setup_texture_bindings(sol::state&);
	rectf get_texture_region(const sol::table& texture);
	rectf get_texture_region(const sol::table& texture, rectf localRegion);
	std::shared_ptr<Texture> get_texture_lease(const sol::table& texture);
}