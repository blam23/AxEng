#include <gtest/gtest.h>

#include "axeng/core/application.h"
#include "axeng/core/lua/bindings/lua_window_bindings.h"

TEST(LuaWindowBindings, DrawPendingIsOnlyBoundOnMainThread)
{
	auto app{ ax::Application::from_embedded({}, ax::EmbeddedResourceLayout{}) };
	sol::state state;
	state.open_libraries(sol::lib::base);
	auto app_table{ state.create_table() };
	auto window_table{ state.create_table() };
	app_table["thread"] = "background";
	app_table["window"] = window_table;
	state["app"] = app_table;

	ax::lua::bindings::setup_window_main_thread_bindings(app, state);
	const auto background_result{ state.do_string("assert(app.window.draw_pending == nil)") };
	ASSERT_TRUE(background_result.valid());

	app_table["thread"] = "main";
	ax::lua::bindings::setup_window_main_thread_bindings(app, state);
	const auto main_result{ state.do_string("assert(type(app.window.draw_pending) == 'function')") };
	ASSERT_TRUE(main_result.valid());
}
