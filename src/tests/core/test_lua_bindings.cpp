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
				local c = 2 * vec2:new(3, 4)
				assert(c.x == 6 and c.y == 8)
				local d = 10 - vec2:new(3, 4)
				assert(d.x == 7 and d.y == 6)
				local e = 12 / vec2:new(3, 4)
				assert(e.x == 4 and e.y == 3)
				assert(vec2:new(3, 4):length() == 5)
				assert(math.abs(vec2:new(3, 4):normalize().x - 0.6) < 0.00001)
				assert(tostring(a):find('vec2', 1, true))

				local f = (vec4:new(1, 2, 3, 4) + 1) * 2 / 4 - vec4:new(1, 1, 1, 1)
				assert(f.x == 0 and f.y == 0.5 and f.z == 1 and f.w == 1.5)
				local g = 2 * vec4:new(1, 2, 3, 4)
				assert(g.x == 2 and g.y == 4 and g.z == 6 and g.w == 8)
				f.w = 2
				assert(f.w == 2)
				assert(tostring(f):find('vec4', 1, true))

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
