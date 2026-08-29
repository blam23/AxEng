#pragma once

// Lua
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

namespace ax
{
	class Script
	{
	public:
		Script();
	private:

		sol::state m_state;
	};
}