#include "axeng/core/lua/bindings/lua_application_bindings.h"

#include "axeng/core/application.h"
#include "axeng/core/event.h"
#include "axeng/core/lua/bindings/lua_resource_bindings.h"
#include "axeng/core/lua/bindings/lua_shared_bindings.h"
#include "axeng/core/lua/bindings/lua_sprite_bindings.h"
#include "axeng/core/lua/bindings/lua_user_io_bindings.h"
#include "axeng/core/lua/bindings/lua_window_bindings.h"
#include "spdlog/spdlog.h"

void ax::lua::bindings::setup_application_bindings(ax::Application& app, sol::state& state, bool mainThread)
{
	auto app_table{ state.create_table() };
	state["app"] = app_table;

	app_table["thread"] = mainThread ? "main" : "background";
	app_table["on_main_thread"] = [mainThread]() { return mainThread; };

	setup_user_io_bindings(app, state);

	if (app.has_window())
	{
		app_table["call_deferred"] =
			[&app](sol::protected_function f)
			{
				app.call_deferred
				(
					[f]()
					{
						const auto res{ f() };
						if (!res.valid())
						{
							const sol::error err = res;
							spdlog::error("Failed to run deferred callback: {}", err.what());
						}
					}
				);
			};

		if (mainThread)
		{
			app.window()->get_resize_event_handler().subscribe
			(
				[&state](const ax::WindowResizeEvent& e)
				{
					auto app_table{ state["app"] };
					auto window_table {app_table["window"].get<sol::table>() };
					window_table["width"] = e.width;
					window_table["height"] = e.height;
				}
			);
		}
	}

	setup_shared_bindings(app, state, mainThread);

	if (app.has_window())
	{
		auto on_update_table{ state.create_table() };
		on_update_table["subscribe"] =
			[&app](sol::protected_function f) -> EventID
			{
				return app.m_window->get_update_event_handler().subscribe
				(
					[f](const ax::WindowUpdateEvent& e)
					{
						const auto res{ f(e.delta) };
						if (!res.valid())
						{
							const sol::error err = res;
							spdlog::error("Failed to run on_update callback: {}", err.what());
						}
					});
			};
		on_update_table["unsubscribe"] =
			[&app](EventID id)
			{
				app.m_window->get_update_event_handler().unsubscribe(id);
			};
		app_table["on_update"] = on_update_table;
	}

	setup_resource_bindings(app, state);
	if (app.has_window())
		setup_sprite_bindings(app, state);

	using EH = EventHandler<sol::object>;

	state.new_usertype<EH>
	(
		"lua_event",
		"subscribe", &EH::subscribe,
		"unsubscribe", &EH::unsubscribe,
		"fire", &EH::fire
	);

	state.new_usertype<Camera>
	(
		"camera",
		"position", sol::property(&Camera::position, &Camera::set_position),
		"zoom", sol::property(&Camera::zoom, &Camera::set_zoom),
		"translate", &Camera::translate
	);

	app_table["current_executable_path"] =
		[]() -> std::string
		{
			char path[MAX_PATH];
			::GetModuleFileNameA(nullptr, path, MAX_PATH);
			return std::string(path);
		};

	bind_conditional_to_state(app, state);

	for (const auto& entry : app.m_conditionalBindings)
	{
		const auto& binding{ entry.second };
		if (binding.first(app, state))
			binding.second(app, state);
	}
}
