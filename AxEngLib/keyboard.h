#pragma once

#include <GLFW/glfw3.h>
#undef APIENTRY

#include <mutex>
#include <vector>

#include "event.h"

namespace ax::input
{
	using Key = int;

	struct KeyEvent
	{
		bool pressed;
		int mods;
	};

	struct AnyKeyEvent
	{
		Key key;
		bool pressed;
		int mods;
	};
	using AnyKeyEventHandler = ax::EventHandler<AnyKeyEvent>;

	class KeyEventHandler final 
		: public ax::EventHandler<KeyEvent>
	{
	public:
		static void register_events(GLFWwindow* window);
		static void cleanup_events(GLFWwindow* window);
		static bool is_key_pressed(Key key);
		inline static AnyKeyEventHandler globalEventHandler{};

		KeyEventHandler(Key key);
		~KeyEventHandler();

	private:
		static void glfw_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
		inline static std::vector<KeyEventHandler*> s_eventHandlers{};
		inline static std::mutex s_handlerMutex{};
		inline static std::vector<bool> s_keyMap{};

		Key m_key;
	};
}