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
	const auto background_result{ state.do_string
	(
		"assert(app.window.draw_pending == nil)\n"
		"assert(app.window.on_frame_image == nil)\n"
		"assert(app.window.on_ui == nil)\n"
		"assert(app.window.on_prepare_frame == nil)\n"
		"assert(app.window.loop_stats == nil)\n"
		"assert(app.window.on_render == nil)\n"
		"assert(app.window.on_close == nil)\n"
		"assert(app.window.on_resize == nil)"
	) };
	ASSERT_TRUE(background_result.valid());

	app_table["thread"] = "main";
	ax::lua::bindings::setup_window_main_thread_bindings(app, state);
	const auto main_result{ state.do_string
	(
		"assert(type(app.window.draw_pending) == 'function')\n"
		"assert(type(app.window.on_frame_image.subscribe) == 'function')\n"
		"assert(type(app.window.on_ui.subscribe) == 'function')\n"
		"assert(type(app.window.on_prepare_frame.subscribe) == 'function')\n"
		"assert(type(app.window.loop_stats) == 'function')\n"
		"assert(type(app.window.on_render.subscribe) == 'function')\n"
		"assert(type(app.window.on_close.subscribe) == 'function')\n"
		"assert(type(app.window.on_resize.subscribe) == 'function')"
	) };
	ASSERT_TRUE(main_result.valid());
}
