#include "lua_bindings.h"
#include "spdlog/spdlog.h"
#include <vector>

bool ax::lua::bindings::register_binding(std::string_view name, std::function<void(sol::state&)> func)
{
	s_binds.push_back(func);
	spdlog::debug("<Lua> Registered lua binding '{}', count: {}", name, s_binds.size());
	return true;
}

bool ax::lua::bindings::register_cleanup(std::string_view name, std::function<void(sol::state&)> func)
{
	s_cleanups.push_back(func);
	spdlog::debug("<Lua> Registered lua cleanup '{}', count: {}", name, s_cleanups.size());
	return true;
}

void ax::lua::bindings::bind_to_state(sol::state& state)
{
	spdlog::debug("<Lua> Setting up all lua bindings, count: {}", s_binds.size());
	for (const auto& bind : s_binds)
		bind(state);
}

void ax::lua::bindings::cleanup_state(sol::state& state)
{
	spdlog::debug("<Lua> Cleaning up lua binds, count: {}", s_cleanups.size());
	for (const auto& cu : s_cleanups)
		cu(state);
}
