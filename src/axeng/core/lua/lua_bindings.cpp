#include "axeng/core/lua/lua_bindings.h"
#include "spdlog/spdlog.h"
#include <vector>

bool ax::lua::bindings::register_binding(std::string_view name, Predicate predicate, BindingCallback bind)
{
	s_binds.emplace_back(predicate, bind);
	spdlog::debug("<Lua> Registered lua binding '{}', count: {}", name, s_binds.size());
	return true;
}

bool ax::lua::bindings::register_cleanup(std::string_view name, Predicate predicate, BindingCallback bind)
{
	s_cleanups.emplace_back(predicate, bind);
	spdlog::debug("<Lua> Registered lua cleanup '{}', count: {}", name, s_cleanups.size());
	return true;
}

bool ax::lua::bindings::register_binding(std::string_view name, std::function<void(sol::state&)> bind)
{
	s_stateBinds.push_back(std::move(bind));
	spdlog::debug("<Lua> Registered lua binding '{}', count: {}", name, s_stateBinds.size());
	return true;
}

bool ax::lua::bindings::register_cleanup(std::string_view name, std::function<void(sol::state&)> bind)
{
	s_stateCleanups.push_back(std::move(bind));
	spdlog::debug("<Lua> Registered lua cleanup '{}', count: {}", name, s_stateCleanups.size());
	return true;
}

void ax::lua::bindings::bind_conditional_to_state(Application& app, sol::state& state)
{
	spdlog::debug("<Lua> Setting up conditional lua bindings, count: {}", s_binds.size());
	for (const auto& bind : s_binds)
		if (bind.first(app, state))
			bind.second(app, state);
}

void ax::lua::bindings::bind_to_state(sol::state& state)
{
	spdlog::debug("<Lua> Setting up lua state bindings, count: {}", s_stateBinds.size());
	for (const auto& bind : s_stateBinds)
		bind(state);
}

void ax::lua::bindings::cleanup_state(Application& app, sol::state& state)
{
	spdlog::debug("<Lua> Cleaning up lua binds, count: {}", s_stateCleanups.size() + s_cleanups.size());
	for (const auto& cleanup : s_stateCleanups)
		cleanup(state);
	for (const auto& cu : s_cleanups)
		if (cu.first(app, state))
			cu.second(app, state);
}
