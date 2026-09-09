#pragma once

#include "lua_engine.h"

namespace ax::lua::bindings
{
	void setup_all(sol::state& env);
	bool register_binding(std::string_view name, std::function<void(sol::state&)> binds);

	inline static std::vector<std::function<void(sol::state&)>> s_binds{};
}
