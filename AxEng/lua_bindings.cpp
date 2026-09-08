#include "lua_bindings.h"
#include "spdlog/spdlog.h"
#include <vector>

std::vector<std::function<void(sol::state&)>> binds{};

bool ax::lua::bindings::register_binding(std::function<void(sol::state&)> func)
{
	binds.push_back(func);
	spdlog::info("Registering lua binding, count: {}", binds.size());
	return true;
}

void ax::lua::bindings::setup_all(sol::state& state)
{
	spdlog::info("Setting up all lua bindings, count: {}", binds.size());
	for (const auto& bind : binds)
		bind(state);
}