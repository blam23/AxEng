#include <gtest/gtest.h>

#include "axeng/core/lua/bindings/bind_all.h"
#include "axeng/core/lua/lua_bindings.h"

TEST(LuaBindings, RepeatedSetupPreservesBindingsInFreshStates)
{
	for (int application = 0; application < 3; ++application)
	{
		SCOPED_TRACE(application);
		ax::lua::bindings::setup();

		for (int thread = 0; thread < 2; ++thread)
		{
			SCOPED_TRACE(thread);
			sol::state state;
			state.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string);
			ax::lua::bindings::bind_to_state(state);

			const auto result{ state.do_string(R"(
				local a = vec2:new(3, 4)
				assert(a.x == 3 and a.y == 4)
				a.x = 6
				assert(a.x == 6)
				local b = (a + vec2:new(2, 4)) * 2 / 4 - 1
				assert(b.x == 3 and b.y == 3)
				assert(vec2:new(3, 4):length() == 5)
				assert(math.abs(vec2:new(3, 4):normalize().x - 0.6) < 0.00001)
				assert(tostring(a):find('vec2', 1, true))

				local c = (vec4:new(1, 2, 3, 4) + 1) * 2 / 4 - vec4:new(1, 1, 1, 1)
				assert(c.x == 0 and c.y == 0.5 and c.z == 1 and c.w == 1.5)
				c.w = 2
				assert(c.w == 2)
				assert(tostring(c):find('vec4', 1, true))

				assert(type(error_code.Success) == 'number')
				assert(type(log.info) == 'function')
				assert(type(custom_type.define) == 'function')
				assert(type(custom_type_size) == 'table')
				assert(type(keyboard.subscribe_key) == 'function')
				assert(type(keyboard.modifier.shift) == 'number')
				assert(type(mouse.subscribe_move) == 'function')
				assert(type(mouse.buttons.left) == 'number')
				assert(type(ui) == 'table')
			)", "@test_repeated_binding_setup") };
			if (!result.valid())
			{
				const sol::error error = result;
				FAIL() << error.what();
			}
		}
	}
}
