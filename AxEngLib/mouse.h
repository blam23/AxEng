#pragma once

#pragma once

#include <GLFW/glfw3.h>
#undef APIENTRY

#include <mutex>
#include <vector>

#include "event.h"

#include "glm/glm.hpp"

namespace ax::input
{
	struct MouseButtonEvent
	{
		bool pressed;
		int button;
	};

	void set_cursor(GLFWwindow* window, int cursorShape);

	using MouseButton = int;

	class MouseButtonEventHandler final
		: public ax::EventHandler<MouseButtonEvent>
	{
	public:
		static void register_events(GLFWwindow* window);
		static void cleanup_events(GLFWwindow* window);
		static bool is_button_pressed(MouseButton button);

		MouseButtonEventHandler(MouseButton button);
		~MouseButtonEventHandler();

	private:
		static void glfw_callback(GLFWwindow* window, int button, int action, int mods);
		inline static std::vector<MouseButtonEventHandler*> s_eventHandlers{};
		inline static std::mutex s_handlerMutex{};
		inline static std::vector<bool> s_buttonMap{};

		MouseButton m_button;
	};

	struct MouseMoveEvent
	{
		float x;
		float y;
	};

	class MouseMoveEventHandler final
		: public ax::EventHandler<MouseMoveEvent>
	{
	public:
		static void register_events(GLFWwindow* window);
		static void cleanup_events(GLFWwindow* window);
		static glm::vec2 get_position();

		MouseMoveEventHandler();
		~MouseMoveEventHandler();

	private:
		static void glfw_callback(GLFWwindow* window, double xpos, double ypos);
		inline static std::vector<MouseMoveEventHandler*> s_eventHandlers{};
		inline static std::mutex s_handlerMutex{};
		inline static std::vector<bool> s_moverMap{};
		inline static glm::vec2 s_position{};
	};
}