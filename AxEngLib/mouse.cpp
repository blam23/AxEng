#include "mouse.h"

#include "spdlog/spdlog.h"

void ax::input::MouseButtonEventHandler::register_events(GLFWwindow* window)
{
	s_buttonMap.resize(GLFW_MOUSE_BUTTON_LAST + 1);
	glfwSetMouseButtonCallback(window, glfw_callback);
}

void ax::input::MouseButtonEventHandler::cleanup_events(GLFWwindow* window)
{
	glfwSetMouseButtonCallback(window, nullptr);
}

bool ax::input::MouseButtonEventHandler::is_button_pressed(MouseButton button)
{
	return s_buttonMap[button];
}

ax::input::MouseButtonEventHandler::MouseButtonEventHandler(MouseButton button)
	: m_button{ button }
{
	std::lock_guard lock{ s_handlerMutex };
	s_eventHandlers.push_back(this);
}

ax::input::MouseButtonEventHandler::~MouseButtonEventHandler()
{
	std::lock_guard lock{ s_handlerMutex };
	std::erase(s_eventHandlers, this);
}

void ax::input::MouseButtonEventHandler::glfw_callback(GLFWwindow*, int button, int action, [[maybe_unused]] int mods)
{
	SPDLOG_TRACE("MouseButtonEventHandler: {}, action: {}, mods: {}", button, action, mods);

	if (button > GLFW_MOUSE_BUTTON_LAST)
	{
		spdlog::error("Unknown mouse button pressed: {}", button);
		return;
	}

	s_buttonMap[button] = action == GLFW_PRESS;

	for (const auto& handler : s_eventHandlers)
	{
		if (handler == nullptr)
		{
			spdlog::error("Null mouse handler found in event handler list");
			continue;
		}

		if (handler->m_button == button)
			handler->fire({ .pressed = action == GLFW_PRESS, .button = button });
	}
}

void ax::input::MouseMoveEventHandler::register_events(GLFWwindow* window)
{
	glfwSetCursorPosCallback(window, glfw_callback);
}

void ax::input::MouseMoveEventHandler::cleanup_events(GLFWwindow* window)
{
	glfwSetCursorPosCallback(window, nullptr);
}

glm::vec2 ax::input::MouseMoveEventHandler::get_position()
{
	return s_position;
}

ax::input::MouseMoveEventHandler::MouseMoveEventHandler()
{
	std::lock_guard lock{ s_handlerMutex };
	s_eventHandlers.push_back(this);
}

ax::input::MouseMoveEventHandler::~MouseMoveEventHandler()
{
	std::lock_guard lock{ s_handlerMutex };
	std::erase(s_eventHandlers, this);
}

void ax::input::MouseMoveEventHandler::glfw_callback(GLFWwindow*, double xpos, double ypos)
{
	SPDLOG_TRACE("MouseMoveEventHandler: xpos: {}, ypos: {}", xpos, ypos);

	s_position = { static_cast<float>(xpos), static_cast<float>(ypos) };

	for (const auto& handler : s_eventHandlers)
	{
		if (handler == nullptr)
		{
			spdlog::error("Null mouse move handler found in event handler list");
			continue;
		}

		handler->fire({ .x = static_cast<float>(xpos), .y = static_cast<float>(ypos) });
	}
}

static int s_lastCursorShape{ -1 };
static GLFWcursor* s_lastCursor{ nullptr };
void ax::input::set_cursor(GLFWwindow* window, int cursorShape)
{
	if (cursorShape == s_lastCursorShape)
		return;

	GLFWcursor* newCursor{ glfwCreateStandardCursor(cursorShape) };
	glfwSetCursor(window, newCursor);

	if (s_lastCursor)
	{
		glfwDestroyCursor(s_lastCursor);
		s_lastCursor = nullptr;
	}

	s_lastCursor = newCursor;
	s_lastCursorShape = cursorShape;
}
