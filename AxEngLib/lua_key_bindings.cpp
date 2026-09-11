#include "lua_key_bindings.h"

#include "keyboard.h"
#include "spdlog/spdlog.h"
#include <vector>
#include <mutex>

std::vector<std::unique_ptr<ax::input::KeyEventHandler>> s_handlers{};
std::mutex s_handlerMutex{};

void ax::lua::bindings::setup_key_bindings(sol::state& env)
{
	auto key_table = env.create_table();

	key_table["register"] =
		[](int key, const std::function<void(bool pressed, int mods)>& callback)
		{
			std::lock_guard lock{ s_handlerMutex };

			spdlog::debug("Registering key: {}", key);
			auto ptr{ std::make_unique<ax::input::KeyEventHandler>(key) };
			ptr->subscribe
			(
				[callback](const auto& e) 
				{
					callback(e.pressed, e.mods); 
				}
			);

			s_handlers.push_back(std::move(ptr));
		};

	env["keyboard"] = key_table;
}

void ax::lua::bindings::cleanup_key_bindings(sol::state&)
{
	std::lock_guard lock{ s_handlerMutex };
	s_handlers.clear();
}
