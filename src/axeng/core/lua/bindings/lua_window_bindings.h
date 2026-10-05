#pragma once

#include "axeng/core/lua/lua_bindings.h"
#include "axeng/core/forward.h"
#include "spdlog/spdlog.h"

namespace ax::lua::bindings
{
	bool window_predicate(ax::Application&, sol::state&);
	void setup_window_bindings(ax::Application&, sol::state&);

	bool window_embed_predicate(ax::Application&, sol::state&);
	void setup_window_embed_bindings(ax::Application&, sol::state&);
}