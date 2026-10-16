#pragma once

#include "axeng/core/lua/lua_bindings.h"
#include "axeng/core/forward.h"
#include "spdlog/spdlog.h"

namespace ax::lua::bindings
{
	bool window_predicate(ax::Application&, sol::state&);
	// Lua: app.window.render_to(texture, function(canvas) canvas:draw_texture(...); canvas:draw_shapes(...) end)
	void setup_window_bindings(ax::Application&, sol::state&);
	void setup_window_main_thread_bindings(ax::Application&, sol::state&);

	bool window_embed_predicate(ax::Application&, sol::state&);
	void setup_window_embed_bindings(ax::Application&, sol::state&);
}