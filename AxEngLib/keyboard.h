#pragma once

#include <GLFW/glfw3.h>
#undef APIENTRY

#include <mutex>
#include <vector>

#include "event.h"

namespace ax::input
{
	struct KeyEvent
	{
		bool pressed;
		int mods;
	};

	class KeyEventHandler final 
		: public ax::EventHandler<KeyEvent>
	{
	public:
		static void register_callback(GLFWwindow* window);

		KeyEventHandler(int key);
		~KeyEventHandler();

	private:
		static void glfw_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
		inline static std::vector<KeyEventHandler*> s_eventHandlers{};
		inline static std::mutex s_handlerMutex{};
		inline static std::vector<bool> s_keyMap{};

		int m_key;
	};
}