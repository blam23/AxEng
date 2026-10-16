#include "axeng/core/lua/bindings/lua_texture_bindings.h"

#include "axeng/core/texture.h"

std::shared_ptr<ax::Texture> ax::lua::bindings::get_texture_lease(const sol::table& texture)
{
	const sol::optional<std::shared_ptr<LuaTextureLease>> lease{ texture["_lease"] };
	return lease ? (*lease)->texture : std::shared_ptr<ax::Texture>{};
}

ax::rectf ax::lua::bindings::get_texture_region(const sol::table& texture)
{
	const sol::optional<sol::table> atlasRegion{ texture["atlas_region"] };
	if (atlasRegion)
	{
		return {
			atlasRegion->get<float>(1), atlasRegion->get<float>(2),
			atlasRegion->get<float>(3), atlasRegion->get<float>(4)
		};
	}

	return { 0.0f, 0.0f, texture.get<float>("width"), texture.get<float>("height") };
}

ax::rectf ax::lua::bindings::get_texture_region(const sol::table& texture, rectf localRegion)
{
	const sol::optional<sol::table> atlasRegion{ texture["atlas_region"] };
	if (atlasRegion)
	{
		localRegion.x += atlasRegion->get<float>(1);
		localRegion.y += atlasRegion->get<float>(2);
	}
	return localRegion;
}

void ax::lua::bindings::setup_texture_bindings(sol::state& state)
{
	state.new_usertype<LuaTextureLease>("texture_lease", sol::no_constructor);

	// kinda buggy
	//state.new_usertype<ax::Texture>
	//(
	//	"texture",
	//	"ui_view", &ax::Texture::imgui_view,
	//	"width", &ax::Texture::width,
	//	"height", &ax::Texture::height
	//);
}
