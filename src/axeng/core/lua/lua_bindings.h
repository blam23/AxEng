#pragma once

#include "axeng/core/lua/lua_engine.h"

#include <functional>
#include <string_view>
#include <utility>
#include <vector>

namespace ax::lua::bindings
{
	using Predicate = std::function<bool(Application&, sol::state&)>;
	using BindingCallback = std::function<void(Application&, sol::state&)>;
	using ConditionalBinding = std::pair<Predicate, BindingCallback>;

	void bind_to_state(Application&, sol::state&);
	void bind_to_state(sol::state&);
	void bind_conditional_to_state(Application&, sol::state&);
	void cleanup_state(Application&, sol::state&);
	void cleanup_state(sol::state&);
	bool register_binding(std::string_view name, Predicate predicate, BindingCallback bind);
	bool register_cleanup(std::string_view name, Predicate predicate, BindingCallback bind);
	bool register_binding(std::string_view name, std::function<void(sol::state&)> bind);
	bool register_cleanup(std::string_view name, std::function<void(sol::state&)> bind);

	inline static std::vector<ConditionalBinding> s_binds{};
	inline static std::vector<ConditionalBinding> s_cleanups{};
	inline static std::vector<std::function<void(sol::state&)>> s_stateBinds{};
	inline static std::vector<std::function<void(sol::state&)>> s_stateCleanups{};
}
