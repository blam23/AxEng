#include "lua_bindings.h"
#include <vector>

std::vector<std::function<void(sol::state&)>> binds{};

bool ax::lua::bindings::register_binding(std::function<void(sol::state&)> func)
{
	binds.push_back(func);
	return true;
}

void ax::lua::bindings::setup_all(sol::state& state)
{
	for (const auto& bind : binds)
		bind(state);
}