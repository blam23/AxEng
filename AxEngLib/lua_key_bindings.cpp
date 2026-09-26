#include "lua_key_bindings.h"

#include "keyboard.h"
#include "spdlog/spdlog.h"
#include <vector>
#include <mutex>

std::vector<std::unique_ptr<ax::input::KeyEventHandler>> s_keyHandlers{};
std::mutex s_keyHandlerMutex{};

void ax::lua::bindings::setup_key_bindings(sol::state& state)
{
	auto key_table = state.create_table();
	auto mod_table = state.create_table();

	mod_table["shift"] = GLFW_MOD_SHIFT;
	mod_table["ctrl"] = GLFW_MOD_CONTROL;
	mod_table["alt"] = GLFW_MOD_ALT;
	key_table["modifier"] = mod_table;

	key_table["subscribe_key"] =
		[](int key, const std::function<void(bool pressed, int mods)>& callback)
		{
			std::lock_guard lock{ s_keyHandlerMutex };

			spdlog::debug("Registering key: {}", key);
			auto ptr{ std::make_unique<ax::input::KeyEventHandler>(key) };
			ptr->subscribe
			(
				[callback](const auto& e) 
				{
					callback(e.pressed, e.mods); 
				}
			);

			s_keyHandlers.push_back(std::move(ptr));
		};

	key_table["subscribe_all"] =
		[](const std::function<void(int key, bool pressed, int mods)>& callback)
		{
			std::lock_guard lock{ s_keyHandlerMutex };

			ax::input::KeyEventHandler::globalEventHandler.subscribe
			(
				[callback](const auto& e)
				{
					callback(e.key, e.pressed, e.mods);
				}
			);
		};

	key_table["is_pressed"] =
		[](ax::input::Key key) -> bool
		{
			return ax::input::KeyEventHandler::is_key_pressed(key);
		};

	state["keyboard"] = key_table;
}

void ax::lua::bindings::cleanup_key_bindings(sol::state&)
{
	std::lock_guard lock{ s_keyHandlerMutex };
	s_keyHandlers.clear();
}
