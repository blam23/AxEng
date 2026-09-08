#include "lua_bindings.h"
#include "spdlog/spdlog.h"
#include <vector>

std::vector<std::function<void(sol::state&)>> binds{};

bool ax::lua::bindings::register_binding(std::string_view name, std::function<void(sol::state&)> func)
{
	binds.push_back(func);
	spdlog::info("Registered lua binding '{}', count: {}", name, binds.size());
	return true;
}

void ax::lua::bindings::setup_all(sol::state& state)
{
	spdlog::info("Setting up all lua bindings, count: {}", binds.size());
	for (const auto& bind : binds)
		bind(state);
}