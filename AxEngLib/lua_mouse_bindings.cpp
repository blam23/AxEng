#include "lua_mouse_bindings.h"

#include "mouse.h"
#include "spdlog/spdlog.h"
#include <vector>
#include <mutex>

std::vector<std::unique_ptr<ax::input::MouseMoveEventHandler>> s_moveHandlers{};
std::vector<std::unique_ptr<ax::input::MouseButtonEventHandler>> s_buttonHandlers{};
std::mutex s_mouseHandlerMutex{};

void ax::lua::bindings::setup_mouse_bindings(sol::state& state)
{
	auto mouse_table = state.create_table();
	auto button_table = state.create_table();
	auto cursor_table = state.create_table();

	button_table["left"] = GLFW_MOUSE_BUTTON_LEFT;
	button_table["right"] = GLFW_MOUSE_BUTTON_RIGHT;
	button_table["middle"] = GLFW_MOUSE_BUTTON_MIDDLE;

	mouse_table["buttons"] = button_table;

	cursor_table["arrow"] = GLFW_ARROW_CURSOR;
	cursor_table["ibeam"] = GLFW_IBEAM_CURSOR;
	cursor_table["crosshair"] = GLFW_CROSSHAIR_CURSOR;
	cursor_table["pointing"] = GLFW_POINTING_HAND_CURSOR;
	cursor_table["hand"] = GLFW_HAND_CURSOR;
	cursor_table["hresize"] = GLFW_HRESIZE_CURSOR;
	cursor_table["vresize"] = GLFW_VRESIZE_CURSOR;
	cursor_table["not_allowed"] = GLFW_NOT_ALLOWED_CURSOR;

	mouse_table["cursors"] = cursor_table;

	mouse_table["get_position"]	=
		[]() -> std::pair<float, float>
		{
			auto pos{ ax::input::MouseMoveEventHandler::get_position() };
			return { pos.x, pos.y };
		};

	mouse_table["subscribe_button"] =
		[](int button, const std::function<void(bool pressed)>& callback)
		{
			std::lock_guard lock{ s_mouseHandlerMutex };

			spdlog::debug("Registering button: {}", button);
			auto ptr{ std::make_unique<ax::input::MouseButtonEventHandler>(button) };
			ptr->subscribe
			(
				[callback](const auto& e)
				{
					callback(e.pressed);
				}
			);

			s_buttonHandlers.push_back(std::move(ptr));
		};

	mouse_table["subscribe_move"] =
		[](const std::function<void(float x, float y)>& callback)
		{
			std::lock_guard lock{ s_mouseHandlerMutex };
			auto ptr{ std::make_unique<ax::input::MouseMoveEventHandler>() };
			ptr->subscribe
			(
				[callback](const auto& e)
				{
					callback(e.x, e.y);
				}
			);

			s_moveHandlers.push_back(std::move(ptr));
		};

	mouse_table["is_pressed"] =
		[](ax::input::MouseButton button) -> bool
		{
			return ax::input::MouseButtonEventHandler::is_button_pressed(button);
		};

	mouse_table["set_cursor"] = 
		[](void* window, int cursor) -> void
		{
			ax::input::set_cursor((GLFWwindow*)window, cursor);
		};

	state["mouse"] = mouse_table;
}

void ax::lua::bindings::cleanup_mouse_bindings(sol::state&)
{
	std::lock_guard lock{ s_mouseHandlerMutex };
	s_moveHandlers.clear();
	s_buttonHandlers.clear();
}
