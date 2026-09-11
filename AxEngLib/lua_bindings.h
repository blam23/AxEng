#pragma once

#include "lua_engine.h"

namespace ax::lua::bindings
{
	void bind_to_state(sol::state&);
	void cleanup_state(sol::state&);
	bool register_binding(std::string_view name, std::function<void(sol::state&)> binds);
	bool register_cleanup(std::string_view name, std::function<void(sol::state&)> binds);

	inline static std::vector<std::function<void(sol::state&)>> s_binds{};
	inline static std::vector<std::function<void(sol::state&)>> s_cleanups{};
}
