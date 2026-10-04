#include "axeng/core/lua/bindings/lua_texture_bindings.h"

#include "axeng/core/texture.h"

void ax::lua::bindings::setup_texture_bindings(sol::state&)
{
	// kinda buggy
	//state.new_usertype<ax::Texture>
	//(
	//	"texture",
	//	"ui_view", &ax::Texture::imgui_view,
	//	"width", &ax::Texture::width,
	//	"height", &ax::Texture::height
	//);
}

