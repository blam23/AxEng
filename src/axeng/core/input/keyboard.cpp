#include "axeng/core/input/keyboard.h"

#include "spdlog/spdlog.h"

void ax::input::KeyEventHandler::register_events(GLFWwindow* window)
{
	s_keyMap.resize(GLFW_KEY_LAST + 1);
	glfwSetKeyCallback(window, glfw_callback);
}

void ax::input::KeyEventHandler::cleanup_events(GLFWwindow* window)
{
	globalEventHandler.unsubscribe_all();
	glfwSetKeyCallback(window, nullptr);
}

bool ax::input::KeyEventHandler::is_key_pressed(Key key)
{
	if (key < 0 || static_cast<std::size_t>(key) >= s_keyMap.size())
		return false;
	return s_keyMap[key];
}

void ax::input::KeyEventHandler::dispatch_external_key(Key key, bool pressed, int mods)
{
	globalEventHandler.fire({ .key = key, .pressed = pressed, .mods = mods });
}


ax::input::KeyEventHandler::KeyEventHandler(Key key)
	: m_key{ key }
{
	std::lock_guard lock{ s_handlerMutex };
	s_eventHandlers.push_back(this);
}

ax::input::KeyEventHandler::~KeyEventHandler()
{
	std::lock_guard lock{ s_handlerMutex };
	std::erase(s_eventHandlers, this);
}

void ax::input::KeyEventHandler::glfw_callback(GLFWwindow*, int key, [[maybe_unused]] int scancode, int action, int mods)
{
	// For now we ignore repeat (when key is held), used more for text input and such.
	if (action == GLFW_REPEAT)
		return;

	SPDLOG_TRACE("KeyEventHandler: {}, scancode: {}, action: {}, mods: {}", key, scancode, action, mods);
	
	if (key < 0 || key > GLFW_KEY_LAST)
	{
		spdlog::debug("Ignoring key event with invalid key code: {}", key);
		return;
	}
	
	s_keyMap[key] = action == GLFW_PRESS;

	globalEventHandler.fire({ .key = key, .pressed = action == GLFW_PRESS, .mods = mods });

	for (const auto& handler : s_eventHandlers)
	{
		if (handler == nullptr)
		{
			spdlog::error("Null key handler found in event handler list");
			continue;
		}

		if (handler->m_key == key)
			handler->fire({ .pressed = action == GLFW_PRESS, .mods = mods });
	}
}
