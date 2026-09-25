#pragma once

#include "keyboard.h"

namespace ax
{
	class Application;
}

namespace ax::debug
{
	class View
	{
	public:
		static void register_debug_view(ax::Application& app);
		static void toggle();
		static void enable();
		static void disable();

	private:
		inline static bool s_enabled{ false };
		inline static ax::input::KeyEventHandler s_toggleHandler{ GLFW_KEY_F11 };
	};
}