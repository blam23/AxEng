#pragma once

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "helpers.h"

namespace ax
{
	class Script
	{
	public:
		DISABLE_COPY_AND_MOVE(Script);

		Script();

		sol::state& get_state()
		{
			return m_state;
		}

		const sol::state& get_state() const
		{
			return m_state;
		}

	private:

		sol::state m_state;
	};
}