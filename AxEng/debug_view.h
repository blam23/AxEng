#pragma once

#include "application.h"

namespace ax::debug
{
	class View
	{
	public:
		static void register_debug_view(ax::Application& app);
		static void enable();
		static void disable();

	private:
		inline static bool m_enabled{ true };
	};
}