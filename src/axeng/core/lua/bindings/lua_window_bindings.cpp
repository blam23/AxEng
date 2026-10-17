#include "axeng/core/lua/bindings/lua_window_bindings.h"

#include "axeng/core/application.h"
#include "axeng/core/lua/bindings/lua_shape_bindings.h"
#include "axeng/core/lua/bindings/lua_texture_bindings.h"
#include "axeng/core/texture.h"
#include "axeng/core/window.h"

#include <cstddef>
#include <filesystem>
#include <format>
#include <string_view>

namespace
{
	struct LuaRenderCanvas
	{
		ax::Window::PendingDrawBatch batch{};
		bool active{ true };
	};

	float number_argument(const sol::variadic_args& args, std::size_t index, std::string_view function)
	{
		if (index >= args.size())
			throw sol::error(std::format("{} is missing argument {}", function, index + 1));
		const auto arg{ args[static_cast<std::ptrdiff_t>(index)] };
		if (arg.get_type() != sol::type::number)
			throw sol::error(std::format("{} argument {} must be a number", function, index + 1));
		return arg.get<float>();
	}

	void record_texture_draw
	(
		LuaRenderCanvas& canvas,
		const sol::table& texture,
		float x,
		float y,
		bool screenSpace,
		const sol::variadic_args& args
	)
	{
		if (!canvas.active)
			throw sol::error("Cannot draw with a render canvas after its render_to callback has returned.");
		if (!texture["valid"])
			throw sol::error("Invalid texture, cannot render");

		const auto keepAlive{ ax::lua::bindings::get_texture_lease(texture) };
		if (texture["temp"].get_or(false) && !keepAlive)
			throw sol::error("Invalid temporary texture, cannot render");

		auto* const tex{ texture["ptr"].get<ax::Texture*>() };
		if (tex == nullptr || tex->view().Get() == nullptr || tex->width() == 0 || tex->height() == 0
			|| tex == canvas.batch.target)
			throw sol::error("Cannot sample an invalid texture or the active render target.");

		auto region{ ax::lua::bindings::get_texture_region(texture) };
		float rotation{};
		float z{};
		glm::vec4 tint{ 1.0f };
		glm::vec2 scale{ 1.0f };
		switch (args.size())
		{
		case 0:
			break;
		case 1:
			z = number_argument(args, 0, "draw_texture");
			break;
		case 4:
		case 5:
			region = ax::lua::bindings::get_texture_region(texture,
				{
					number_argument(args, 0, "draw_texture"),
					number_argument(args, 1, "draw_texture"),
					number_argument(args, 2, "draw_texture"),
					number_argument(args, 3, "draw_texture")
				});
			if (args.size() == 5)
				z = number_argument(args, 4, "draw_texture");
			break;
		case 12:
			rotation = number_argument(args, 0, "draw_texture");
			region = ax::lua::bindings::get_texture_region(texture,
				{
					number_argument(args, 1, "draw_texture"),
					number_argument(args, 2, "draw_texture"),
					number_argument(args, 3, "draw_texture"),
					number_argument(args, 4, "draw_texture")
				});
			z = number_argument(args, 5, "draw_texture");
			tint = {
				number_argument(args, 6, "draw_texture"),
				number_argument(args, 7, "draw_texture"),
				number_argument(args, 8, "draw_texture"),
				number_argument(args, 9, "draw_texture")
			};
			scale = {
				number_argument(args, 10, "draw_texture"),
				number_argument(args, 11, "draw_texture")
			};
			break;
		default:
			throw sol::error("draw_texture expects zero, one, four, five, or twelve optional arguments.");
		}

		ax::SpriteDefinition sprite{};
		sprite.tex = tex;
		sprite.keepAlive = keepAlive;
		sprite.gpuData.pos = { x, y };
		sprite.gpuData.region = region;
		sprite.gpuData.useRegion = 1u;
		sprite.gpuData.rotation = rotation;
		sprite.gpuData.z = z;
		sprite.gpuData.tint = tint;
		sprite.gpuData.scale = scale;
		sprite.gpuData.screenSpace = screenSpace ? 1u : 0u;
		canvas.batch.textures.push_back(std::move(sprite));
	}

	void record_shapes(LuaRenderCanvas& canvas, const sol::object& value)
	{
		if (!canvas.active)
			throw sol::error("Cannot draw with a render canvas after its render_to callback has returned.");

		if (value.is<ax::lua::bindings::LuaShapeList>())
		{
			const auto& list{ value.as<ax::lua::bindings::LuaShapeList>() };
			canvas.batch.shapes.insert(canvas.batch.shapes.end(), list.shapes.begin(), list.shapes.end());
			return;
		}
		if (value.get_type() != sol::type::table)
			throw sol::error("draw_shapes expects a shape_list or shape description table.");

		ax::lua::bindings::shapes_from_table(value.as<sol::table>(), canvas.batch.shapes);
	}

	void render_texture
	(
		ax::Application& app,
		const sol::table& texture,
		float x,
		float y,
		bool useAtlas,
		bool ui,
		const sol::variadic_args& args
	)
	{
		if (!texture["valid"])
		{
			spdlog::error("Invalid texture, cannot render");
			return;
		}

		const auto keepAlive{ ax::lua::bindings::get_texture_lease(texture) };
		if (texture["temp"].get_or(false) && !keepAlive)
		{
			spdlog::error("Invalid temporary texture, cannot render");
			return;
		}
		auto* const tex{ texture["ptr"].get<ax::Texture*>() };
		if (tex == nullptr)
			return;
		auto region{ ax::rectf{} };
		auto hasRegion{ useAtlas };
		float rotation{};
		float z{};
		glm::vec4 tint{ 1.0f };
		glm::vec2 scale{ 1.0f };

		if (useAtlas)
			region = ax::lua::bindings::get_texture_region(texture);

		switch (args.size())
		{
		case 0:
			break;
		case 1:
			z = number_argument(args, 0, ui ? "render_ui" : "render");
			break;
		case 4:
		case 5:
			region = {
				number_argument(args, 0, ui ? "render_ui" : "render"),
				number_argument(args, 1, ui ? "render_ui" : "render"),
				number_argument(args, 2, ui ? "render_ui" : "render"),
				number_argument(args, 3, ui ? "render_ui" : "render")
			};
			hasRegion = true;
			if (useAtlas)
				region = ax::lua::bindings::get_texture_region(texture, region);
			if (args.size() == 5)
				z = number_argument(args, 4, ui ? "render_ui" : "render");
			break;
		case 12:
			region = {
				number_argument(args, 0, ui ? "render_ui" : "render"),
				number_argument(args, 1, ui ? "render_ui" : "render"),
				number_argument(args, 2, ui ? "render_ui" : "render"),
				number_argument(args, 3, ui ? "render_ui" : "render")
			};
			rotation = number_argument(args, 4, ui ? "render_ui" : "render");
			hasRegion = true;
			if (useAtlas)
				region = ax::lua::bindings::get_texture_region(texture, region);
			z = number_argument(args, 5, ui ? "render_ui" : "render");
			tint = {
				number_argument(args, 6, ui ? "render_ui" : "render"),
				number_argument(args, 7, ui ? "render_ui" : "render"),
				number_argument(args, 8, ui ? "render_ui" : "render"),
				number_argument(args, 9, ui ? "render_ui" : "render")
			};
			scale = {
				number_argument(args, 10, ui ? "render_ui" : "render"),
				number_argument(args, 11, ui ? "render_ui" : "render")
			};
			break;
		default:
			throw sol::error(std::format("{} expects zero, one, four, five, or twelve optional arguments.",
				ui ? "render_ui" : "render"));
		}

		if (hasRegion)
		{
			if (ui)
			{
				if (args.size() == 12)
					app.window()->render_ui_texture(tex, { x, y }, rotation, region, z, tint, scale, keepAlive);
				else if (args.size() == 5 || args.size() == 1)
					app.window()->render_ui_texture(tex, { x, y }, region, z, keepAlive);
				else
					app.window()->render_ui_texture(tex, { x, y }, region, keepAlive);
			}
			else if (args.size() == 12)
				app.window()->render_texture(tex, { x, y }, rotation, region, z, tint, scale, keepAlive);
			else if (args.size() == 5 || args.size() == 1)
				app.window()->render_texture(tex, { x, y }, region, z, keepAlive);
			else
				app.window()->render_texture(tex, { x, y }, region, keepAlive);
		}
		else if (ui)
		{
			if (args.size() == 1)
				app.window()->render_ui_texture(tex, { x, y }, z, keepAlive);
			else
				app.window()->render_ui_texture(tex, { x, y }, keepAlive);
		}
		else if (args.size() == 1)
			app.window()->render_texture(tex, { x, y }, z, keepAlive);
		else
			app.window()->render_texture(tex, { x, y }, keepAlive);
	}
}

bool ax::lua::bindings::window_predicate(Application& app, sol::state&)
{
	return app.window() != nullptr;
}

namespace
{
	void setup_window_event_bindings(ax::Application& app, sol::state& state, sol::table& window_table)
	{
		auto on_frame_image_table{ state.create_table() };
		on_frame_image_table["subscribe"] =
			[&app](sol::protected_function f) -> ax::EventID
			{
				return app.window()->get_frame_image_event_handler().subscribe([f](const ax::WindowFrameImageEvent& e)
					{
						const auto res{ f
						(
							e.success,
							e.compared,
							e.matches,
							e.differingPixels,
							e.pixelCount,
							e.maxChannelDifference,
							e.message,
							e.imageData
						) };
						if (!res.valid())
						{
							const sol::error err = res;
							spdlog::error("Failed to run on_frame_image callback: {}", err.what());
						}
					});
			};
		on_frame_image_table["unsubscribe"] =
			[&app](ax::EventID id)
			{
				app.window()->get_frame_image_event_handler().unsubscribe(id);
			};
		window_table["on_frame_image"] = on_frame_image_table;

		auto on_ui_table{ state.create_table() };
		on_ui_table["subscribe"] =
			[&app](sol::protected_function f) -> ax::EventID
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
			[&app](ax::EventID id)
			{
				app.window()->get_ui_event_handler().unsubscribe(id);
			};
		window_table["on_ui"] = on_ui_table;

		auto on_render_table{ state.create_table() };
		on_render_table["subscribe"] =
			[&app](sol::protected_function f) -> ax::EventID
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
			[&app](ax::EventID id)
			{
				app.window()->get_render_event_handler().unsubscribe(id);
			};
		window_table["on_render"] = on_render_table;

		auto on_close_table{ state.create_table() };
		on_close_table["subscribe"] =
			[&app](sol::protected_function f) -> ax::EventID
			{
				return app.window()->get_request_close_event_handler().subscribe([f](const ax::WindowRequestCloseEvent&)
					{
						const auto res{ f() };
						if (!res.valid())
						{
							const sol::error err = res;
							spdlog::error("Failed to run on_close callback: {}", err.what());
						}
					});
			};
		on_close_table["unsubscribe"] =
			[&app](ax::EventID id)
			{
				app.window()->get_request_close_event_handler().unsubscribe(id);
			};
		window_table["on_close"] = on_close_table;

		auto on_resize_table{ state.create_table() };
		on_resize_table["subscribe"] =
			[&app](sol::protected_function f) -> ax::EventID
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
			[&app](ax::EventID id)
			{
				app.window()->get_resize_event_handler().unsubscribe(id);
			};
		window_table["on_resize"] = on_resize_table;
	}
}

void ax::lua::bindings::setup_window_bindings(Application& app, sol::state& state)
{
	state.new_usertype<LuaRenderCanvas>
	(
		"render_canvas",
		sol::no_constructor,
		"draw_texture",
		[](LuaRenderCanvas& canvas, const sol::table& texture, float x, float y, sol::variadic_args args)
		{
			record_texture_draw(canvas, texture, x, y, false, args);
		},
		"draw_ui_texture",
		[](LuaRenderCanvas& canvas, const sol::table& texture, float x, float y, sol::variadic_args args)
		{
			record_texture_draw(canvas, texture, x, y, true, args);
		},
		"draw_shapes",
		[](LuaRenderCanvas& canvas, const sol::object& value)
		{
			record_shapes(canvas, value);
		}
	);

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

	// Borderless fullscreen only
	static std::mutex s_fullscreen_mutex;
	static std::tuple<uint32_t, uint32_t, uint32_t, uint32_t> s_old_size;
	static bool s_fullscreen{ false };
	window_table["set_fullscreen"] =
		[&app](bool fullscreen) -> void
		{
			std::lock_guard<std::mutex> lock(s_fullscreen_mutex);

			if (s_fullscreen == fullscreen)
				return;

			if (fullscreen)
			{
				s_fullscreen = true;
				int x, y;
				glfwGetWindowPos(app.window()->glfw_handle(), &x, &y);
				s_old_size = { x, y, app.window()->width(), app.window()->height() };
				
				GLFWmonitor* monitor{ glfwGetPrimaryMonitor() };
				const GLFWvidmode* mode{ glfwGetVideoMode(monitor) };

				glfwSetWindowAttrib(app.window()->glfw_handle(), GLFW_DECORATED, GLFW_FALSE);
				glfwSetWindowSize(app.window()->glfw_handle(), mode->width, mode->height);
				glfwSetWindowPos(app.window()->glfw_handle(), 0, 0);
			}
			else
			{
				s_fullscreen = false;
				glfwSetWindowAttrib(app.window()->glfw_handle(), GLFW_DECORATED, GLFW_TRUE);
				glfwSetWindowPos(app.window()->glfw_handle(), std::get<0>(s_old_size), std::get<1>(s_old_size));
				glfwSetWindowSize(app.window()->glfw_handle(), std::get<2>(s_old_size), std::get<3>(s_old_size));
			}
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
		[&app](float r, float g, float b)
		{
			app.window()->set_clear_color({ r, g, b });
		};

	window_table["compare_next_frame"] =
		[&app](const sol::table& reference) -> bool
		{
			if (!reference["valid"])
				return false;
			const auto keepAlive{ ax::lua::bindings::get_texture_lease(reference) };
			if (reference["temp"].get_or(false) && !keepAlive)
				return false;
			auto* const texture{ reference["ptr"].get<Texture*>() };
			return texture != nullptr && app.window()->compare_next_frame(*texture, keepAlive);
		};

	if (app.has_permission(lua::Permission::IO))
	{
		window_table["capture_next_frame_io"] =
			[&app](const std::string& path) -> bool
			{
				if (path.empty())
				{
					spdlog::error("Frame capture requires a path.");
					return false;
				}

				return app.window()->capture_next_frame(std::filesystem::path{ path });
			};
	}

	window_table["capture_next_frame_user"] =
		[&app](const std::string& path) -> bool
		{
			if (path.empty())
			{
				spdlog::error("Frame capture requires a path.");
				return false;
			}

			const auto outputPath{ app.user_files().get_user_file_path(path) };
			return outputPath.has_value() && app.window()->capture_next_frame(*outputPath);
		};

	window_table["width"] = app.window()->width();
	window_table["height"] = app.window()->height();

	const bool useAtlas{ app.has_atlas_texture() };
	window_table["render"] =
		[&app, useAtlas](const sol::table& texture, float x, float y, sol::variadic_args args)
		{
			render_texture(app, texture, x, y, useAtlas, false, args);
		};
	window_table["render_ui"] =
		[&app, useAtlas](const sol::table& texture, float x, float y, sol::variadic_args args)
		{
			render_texture(app, texture, x, y, useAtlas, true, args);
		};

	// Each callback records an isolated batch; only successful callbacks reach the render thread.
	window_table["render_to"] =
		[&app](const sol::table& texture, sol::protected_function callback) -> bool
		{
			if (!texture["valid"])
			{
				spdlog::error("Invalid texture, cannot render to it");
				return false;
			}

			const auto targetLease{ ax::lua::bindings::get_texture_lease(texture) };
			if (texture["temp"].get_or(false) && !targetLease)
			{
				spdlog::error("Invalid temporary texture, cannot render to it");
				return false;
			}
			auto* const target{ texture["ptr"].get<Texture*>() };
			if (target == nullptr || target->texture().Get() == nullptr || target->view().Get() == nullptr
				|| target->width() == 0 || target->height() == 0)
			{
				spdlog::error("Cannot render to an invalid texture.");
				return false;
			}

			auto canvas{ std::make_shared<LuaRenderCanvas>() };
			canvas->batch.target = target;
			canvas->batch.targetLease = targetLease;
			const auto result{ callback(canvas) };
			canvas->active = false;
			if (!result.valid())
			{
				const sol::error err{ result };
				spdlog::error("Failed to run render_to callback: {}", err.what());
				canvas->batch = {};
				return false;
			}

			return app.window()->submit_pending_draw_batch(std::move(canvas->batch));
		};

	window_table["screen_contains_region"] =
		[&app](float x, float y, float w, float h) -> bool
		{
			return app.window()->screen_contains_region({ x, y, w, h });
		};

	auto app_table{ state["app"] };
	app_table["window"] = window_table;

	setup_window_main_thread_bindings(app, state);
}

void ax::lua::bindings::setup_window_main_thread_bindings(Application& app, sol::state& state)
{
	auto app_table{ state["app"].get<sol::table>() };

	if (app_table["thread"].get<std::string>() != "main")
		return;

	auto window_table{ app_table["window"].get<sol::table>() };
	setup_window_event_bindings(app, state, window_table);
	window_table["draw_pending"] =
		[&app]()
		{
			app.window()->draw_pending();
		};
}

bool ax::lua::bindings::window_embed_predicate(Application& app, sol::state&)
{
	return app.window() != nullptr && app.has_permission(lua::Permission::OS);
}

void ax::lua::bindings::setup_window_embed_bindings(Application& app, sol::state& state)
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