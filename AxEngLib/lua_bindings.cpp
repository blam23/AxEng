#include "lua_bindings.h"
#include "spdlog/spdlog.h"
#include <vector>

bool ax::lua::bindings::register_binding(std::string_view name, std::function<void(sol::state&)> func)
{
	s_binds.push_back(func);
	spdlog::info("Registered lua binding '{}', count: {}", name, s_binds.size());
	return true;
}

void ax::lua::bindings::setup_all(sol::state& state)
{
	spdlog::info("Setting up all lua bindings, count: {}", s_binds.size());
	for (const auto& bind : s_binds)
		bind(state);
}