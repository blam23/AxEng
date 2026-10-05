#include "axeng/core/lua/bindings/lua_window_bindings.h"

#include "axeng/core/application.h"
#include "axeng/core/lua/bindings/lua_texture_bindings.h"
#include "axeng/core/texture.h"
#include "axeng/core/window.h"

bool ax::lua::bindings::window_predicate(ax::Application& app, sol::state&)
{
	return app.window() != nullptr;
}

void ax::lua::bindings::setup_window_bindings(ax::Application& app, sol::state& state)
{
	auto window_table{ state.create_table() };

	window_table["request_close"] = 
		[&app]() -> void
		{
			glfwSetWindowShouldClose(app.window()->glfw_handle(), true);
		};

	window_table["set_title"] =
		[&app](const std::string& str) -> void
		{
			glfwSetWindowTitle(app.window()->glfw_handle(), str.c_str());
		};

	window_table["handle"] = (void*)app.window()->glfw_handle();

	window_table["camera"] = &app.window()->camera();

	window_table["global_to_viewport"] =
		[&app](float x, float y) -> std::pair<float, float>
		{
			const auto pos{ app.window()->global_to_viewport({ x, y }) };
			return { pos.x, pos.y };
		};

	window_table["viewport_to_global"] =
		[&app](float x, float y) -> std::pair<float, float>
		{
			const auto pos{ app.window()->viewport_to_global({ x, y }) };
			return { pos.x, pos.y };
		};

	window_table["prevent_close"] =
		[&app]()
		{
			app.window()->prevent_close();
		};

	window_table["set_clear_color"] =
		[&app](float r, float g, float b, float a)
		{
			app.window()->set_clear_color({ r, g, b, a });
		};

	auto on_ui_table{ state.create_table() };
	on_ui_table["subscribe"] =
		[&app](sol::protected_function f) -> EventID
		{
			return app.window()->get_ui_event_handler().subscribe([f](const ax::WindowUIEvent& e)
				{
					const auto res{ f(e.delta) };
					if (!res.valid())
					{
						const sol::error err = res;
						spdlog::error("Failed to run on_ui callback: {}", err.what());
					}
				});
		};
	on_ui_table["unsubscribe"] =
		[&app](EventID id)
		{
			app.window()->get_ui_event_handler().unsubscribe(id);
		};
	window_table["on_ui"] = on_ui_table;

	auto on_render_table{ state.create_table() };
	on_render_table["subscribe"] =
		[&app](sol::protected_function f) -> EventID
		{
			return app.window()->get_render_event_handler().subscribe([f](const ax::WindowRenderEvent& e)
				{
					const auto res{ f(e.delta, e.pass) };
					if (!res.valid())
					{
						const sol::error err = res;
						spdlog::error("Failed to run on_render callback: {}", err.what());
					}
				});
		};
	on_render_table["unsubscribe"] =
		[&app](EventID id)
		{
			app.window()->get_render_event_handler().unsubscribe(id);
		};
	window_table["on_render"] = on_render_table;

	auto on_close_table{ state.create_table() };
	on_close_table["subscribe"] =
		[&app](sol::protected_function f) -> EventID
		{
			return app.window()->get_request_close_event_handler().subscribe([f](const ax::WindowRequestCloseEvent&) {
				const auto res{ f() };
				if (!res.valid())
				{
					const sol::error err = res;
					spdlog::error("Failed to run on_close callback: {}", err.what());
				}
			});
		};
	on_close_table["unsubscribe"] =
		[&app](EventID id)
		{
			app.window()->get_request_close_event_handler().unsubscribe(id);
		};
	window_table["on_close"] = on_close_table;

	auto on_resize_table{ state.create_table() };
	on_resize_table["subscribe"] =
		[&app](sol::protected_function f) -> EventID
		{
			return app.window()->get_resize_event_handler().subscribe([f](const ax::WindowResizeEvent& e)
				{
					const auto res{ f(e.width, e.height) };
					if (!res.valid())
					{
						const sol::error err = res;
						spdlog::error("Failed to run on_resize callback: {}", err.what());
					}
				});
		};
	on_resize_table["unsubscribe"] =
		[&app](EventID id)
		{
			app.window()->get_resize_event_handler().unsubscribe(id);
		};
	window_table["on_resize"] = on_resize_table;

	window_table["width"] = app.window()->width();
	window_table["height"] = app.window()->height();

	if (app.has_atlas_texture())
	{
		window_table["render"] =
			sol::overload
			(
				[&app](const sol::table& texture, float x, float y)
				{
					if (texture["valid"])
						app.window()->render_texture(texture["ptr"].get<Texture*>(), { x, y }, lua::bindings::get_texture_region(texture));
					else
						spdlog::error("Invalid texture, cannot render");
				},
				[&app](const sol::table& texture, float x, float y, float z)
				{
					if (texture["valid"])
						app.window()->render_texture(texture["ptr"].get<Texture*>(), { x, y }, lua::bindings::get_texture_region(texture), z);
					else
						spdlog::error("Invalid texture, cannot render");
				},
				[&app](const sol::table& texture, float x, float y, float ax, float ay, float aw, float ah)
				{
					if (texture["valid"])
					{
						const auto t{ texture["ptr"].get<ax::Texture*>() };
						app.window()->render_texture(t, { x, y }, lua::bindings::get_texture_region(texture, { ax, ay, aw, ah }));
					}
					else
					{
						spdlog::error("Invalid texture, cannot render");
					}
				},
				[&app](const sol::table& texture, float x, float y, float ax, float ay, float aw, float ah, float z)
				{
					if (texture["valid"])
					{
						const auto t{ texture["ptr"].get<ax::Texture*>() };
						app.window()->render_texture(t, { x, y }, lua::bindings::get_texture_region(texture, { ax, ay, aw, ah }), z);
					}
					else
					{
						spdlog::error("Invalid texture, cannot render");
					}
				},
				[&app](const sol::table& texture, float x, float y, float ax, float ay, float aw, float ah,
					float r, float z, float cr, float cg, float cb, float ca, float sx, float sy)
				{
					if (texture["valid"])
					{
						const auto t{ texture["ptr"].get<ax::Texture*>() };
						app.window()->render_texture(t, { x, y }, r, lua::bindings::get_texture_region(texture, { ax, ay, aw, ah }), z, { cr, cg, cb, ca }, { sx, sy });
					}
					else
					{
						spdlog::error("Invalid texture, cannot render");
					}
				}
			);

		window_table["render_ui"] =
			sol::overload
			(
				[&app](const sol::table& texture, float x, float y)
				{
					if (texture["valid"])
						app.window()->render_ui_texture(texture["ptr"].get<Texture*>(), { x, y }, lua::bindings::get_texture_region(texture));
					else
						spdlog::error("Invalid texture, cannot render");
				},
				[&app](const sol::table& texture, float x, float y, float z)
				{
					if (texture["valid"])
						app.window()->render_ui_texture(texture["ptr"].get<Texture*>(), { x, y }, lua::bindings::get_texture_region(texture), z);
					else
						spdlog::error("Invalid texture, cannot render");
				},
				[&app](const sol::table& texture, float x, float y, float ax, float ay, float aw, float ah)
				{
					if (texture["valid"])
					{
						const auto t{ texture["ptr"].get<ax::Texture*>() };
						app.window()->render_ui_texture(t, { x, y }, lua::bindings::get_texture_region(texture, { ax, ay, aw, ah }));
					}
					else
					{
						spdlog::error("Invalid texture, cannot render");
					}
				},
				[&app](const sol::table& texture, float x, float y, float ax, float ay, float aw, float ah, float z)
				{
					if (texture["valid"])
					{
						const auto t{ texture["ptr"].get<ax::Texture*>() };
						app.window()->render_ui_texture(t, { x, y }, lua::bindings::get_texture_region(texture, { ax, ay, aw, ah }), z);
					}
					else
					{
						spdlog::error("Invalid texture, cannot render");
					}
				},
				[&app](const sol::table& texture, float x, float y, float ax, float ay, float aw, float ah,
					float r, float z, float cr, float cg, float cb, float ca, float sx, float sy)
				{
					if (texture["valid"])
					{
						const auto t{ texture["ptr"].get<ax::Texture*>() };
						app.window()->render_ui_texture(t, { x, y }, r, lua::bindings::get_texture_region(texture, { ax, ay, aw, ah }), z, { cr, cg, cb, ca }, { sx, sy });
					}
					else
					{
						spdlog::error("Invalid texture, cannot render");
					}
				}
			);
	}
	else
	{
		window_table["render"] =
			sol::overload
			(
				[&app](const sol::table& texture, float x, float y)
				{
					if (texture["valid"])
						app.window()->render_texture(texture["ptr"].get<Texture*>(), { x, y });
					else
						spdlog::error("Invalid texture, cannot render");
				},
				[&app](const sol::table& texture, float x, float y, float z)
				{
					if (texture["valid"])
						app.window()->render_texture(texture["ptr"].get<Texture*>(), { x, y }, z);
					else
						spdlog::error("Invalid texture, cannot render");
				},
				[&app](const sol::table& texture, float x, float y, float ax, float ay, float aw, float ah)
				{
					if (texture["valid"])
					{
						const auto t{ texture["ptr"].get<ax::Texture*>() };
						app.window()->render_texture(t, { x, y }, { ax, ay, aw, ah });
					}
					else
					{
						spdlog::error("Invalid texture, cannot render");
					}
				},
				[&app](const sol::table& texture, float x, float y, float ax, float ay, float aw, float ah, float z)
				{
					if (texture["valid"])
					{
						const auto t{ texture["ptr"].get<ax::Texture*>() };
						app.window()->render_texture(t, { x, y }, { ax, ay, aw, ah }, z);
					}
					else
					{
						spdlog::error("Invalid texture, cannot render");
					}
				},
				[&app](const sol::table& texture, float x, float y, float ax, float ay, float aw, float ah,
					float r, float z, float cr, float cg, float cb, float ca, float sx, float sy)
				{
					if (texture["valid"])
					{
						const auto t{ texture["ptr"].get<ax::Texture*>() };
						app.window()->render_texture(t, { x, y }, r, { ax, ay, aw, ah }, z, { cr, cg, cb, ca }, { sx, sy });
					}
					else
					{
						spdlog::error("Invalid texture, cannot render");
					}
				}
			);

		window_table["render_ui"] =
			sol::overload
			(
				[&app](const sol::table& texture, float x, float y)
				{
					if (texture["valid"])
						app.window()->render_ui_texture(texture["ptr"].get<Texture*>(), { x, y });
					else
						spdlog::error("Invalid texture, cannot render");
				},
				[&app](const sol::table& texture, float x, float y, float z)
				{
					if (texture["valid"])
						app.window()->render_ui_texture(texture["ptr"].get<Texture*>(), { x, y }, z);
					else
						spdlog::error("Invalid texture, cannot render");
				},
				[&app](const sol::table& texture, float x, float y, float ax, float ay, float aw, float ah)
				{
					if (texture["valid"])
					{
						const auto t{ texture["ptr"].get<ax::Texture*>() };
						app.window()->render_ui_texture(t, { x, y }, { ax, ay, aw, ah });
					}
					else
					{
						spdlog::error("Invalid texture, cannot render");
					}
				},
				[&app](const sol::table& texture, float x, float y, float ax, float ay, float aw, float ah, float z)
				{
					if (texture["valid"])
					{
						const auto t{ texture["ptr"].get<ax::Texture*>() };
						app.window()->render_ui_texture(t, { x, y }, { ax, ay, aw, ah }, z);
					}
					else
					{
						spdlog::error("Invalid texture, cannot render");
					}
				},
				[&app](const sol::table& texture, float x, float y, float ax, float ay, float aw, float ah,
					float r, float z, float cr, float cg, float cb, float ca, float sx, float sy)
				{
					if (texture["valid"])
					{
						const auto t{ texture["ptr"].get<ax::Texture*>() };
						app.window()->render_ui_texture(t, { x, y }, r, { ax, ay, aw, ah }, z, { cr, cg, cb, ca }, { sx, sy });
					}
					else
					{
						spdlog::error("Invalid texture, cannot render");
					}
				}
			);
	}

	auto app_table{ state["app"] };
	app_table["window"] = window_table;
}

bool ax::lua::bindings::window_embed_predicate(ax::Application& app, sol::state&)
{
	return app.window() != nullptr && app.has_permission(lua::Permission::OS);
}

void ax::lua::bindings::setup_window_embed_bindings(ax::Application& app, sol::state& state)
{
	auto app_table{ state["app"] };
	auto window_table {app_table["window"].get<sol::table>() };

	window_table["try_embed_child"] =
		[&app](float procIdFloat) -> bool
		{
			DWORD processID = static_cast<DWORD>(procIdFloat);
			return app.window()->try_embed_child(processID);
		};

	window_table["set_embedded_child_position"] =
		[&app](float procIdFloat, float x, float y, float width, float height)
		{
			DWORD processID = static_cast<DWORD>(procIdFloat);
			app.window()->set_embedded_child_position(processID, static_cast<int>(x), static_cast<int>(y), static_cast<int>(width), static_cast<int>(height));
		};

	window_table["try_kill_child"] =
		[&app](float procIdFloat)
		{
			DWORD processID = static_cast<DWORD>(procIdFloat);
			app.window()->try_kill_child(processID);
		};

	window_table["focus_child"] =
		[&app](float procIdFloat)
		{
			DWORD processID = static_cast<DWORD>(procIdFloat);
			return app.window()->focus_child(processID);
		};

	window_table["redirect_input_to_child"] =
		[&app](float procIdFloat)
		{
			DWORD processID = static_cast<DWORD>(procIdFloat);
			app.window()->redirect_input_to_child(processID);
		};

	window_table["reset_input_redirection"] =
		[&app]()
		{
			app.window()->reset_input_redirection();
		};
}