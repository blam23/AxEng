#include "application.h"
#include "vulkan_context.h"
#include "lua_engine.h"

#include "lua_lib_loader.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/string_cast.hpp"

ax::Application ax::Application::from_directory(std::string_view root)
{
	return { DirectoryResourceLoader{ root } };
}

ax::Application ax::Application::from_embedded(EmbeddedResourceLayout&& layout)
{
	return { std::move(layout) };
}

ax::Application ax::Application::from_zip(std::string_view zipFile)
{
	return { ZipResourceLoader{ zipFile } };
}

bool ax::Application::init_window()
{
	LogTimer _timer{ "Vulkan initial setup" };

	const auto& app{ m_env["app"] };
	const auto& window{ app["window"] };
	m_window = std::make_unique<Window>(WindowDefinition
	{
		.width = window["width"],
		.height = window["height"],
		.title = window["title"],
		.vsync = window["vsync"],
	});

	bool success{ true };
	success = m_window->init_vulkan();
	if (!success)
		return false;

	m_textures.set_context(m_window->vulkan_context());

	success = m_window->init_imgui();
	return success;
}

void ax::Application::add_application_bindings(sol::state& state)
{
	const auto& app{ m_env["app"] };

	{
		auto on_update_table{ state.create_table() };
		on_update_table["subscribe"] =
			[this](std::function<void(double delta)> f) -> size_t
			{
				return m_window->get_update_event_handler().subscribe([f](const ax::WindowUpdateEvent& e) { f(e.delta); });
			};
		on_update_table["unsubscribe"] =
			[this](size_t id)
			{
				m_window->get_update_event_handler().unsubscribe(id);
			};
		app["on_update"] = on_update_table;
	}

	{
		auto resource_lookup_texture{ state.create_table() };

		resource_lookup_texture["get_texture"] =
			[this, &state](const std::string& name) -> sol::table
			{
				auto text{ m_textures.get(name) };

				auto ret = state.create_table();

				if (text)
				{
					ret["valid"] = true;
					ret["ptr"] = text;
					ret["ui_view"] = text->imgui_view();
					ret["width"] = text->width();
					ret["height"] = text->height();
				}
				else
				{
					ret["valid"] = false;
				}

				return ret;
			};

		resource_lookup_texture["create_texture"] =
			[this, &state](const std::string& name, const std::string& data) -> sol::table
			{
				auto text{ m_textures.get(name) };

				if (text == nullptr)
					text = m_textures.load_from_raw(name, {}, std::vector<uint8_t>{ data.begin(), data.end() });

				auto ret = state.create_table();

				if (text)
				{
					ret["valid"] = true;
					ret["ptr"] = text;
					ret["ui_view"] = text->imgui_view();
					ret["width"] = text->width();
					ret["height"] = text->height();
				}
				else
				{
					ret["valid"] = false;
				}

				return ret;
			};

		resource_lookup_texture["get_script"] =
			[this, &state](const std::string& name) -> sol::table
			{
				auto script{ m_scripts.get(name) };

				auto ret = state.create_table();

				if (script)
				{
					ret["valid"] = true;
					ret["ptr"] = script;
				}
				else
				{
					ret["valid"] = false;
				}

				return ret;
			};

		app["res"] = resource_lookup_texture;
	}

	app["run_in_this_environment"] = 
		[this](const sol::table& script) -> sol::object
		{
			if (script["valid"])
			{
				auto ptr{ script["ptr"].get<ax::lua::Script*>() };
				if (ptr == nullptr)
				{
					spdlog::error("Invalid script object, cannot run");
					return nullptr;
				}
				auto res{ ptr->run_no_cache(m_env) };
				if (!res.valid())
				{
					const sol::error msg = res;
					spdlog::error("Failed to run script: {}", msg.what());
				}
				return res;
			}
			else
			{
				spdlog::error("Invalid script, cannot run");
				return nullptr;
			}
		};

	if (m_create_window)
	{
		const auto& window{ app["window"] };

		window["prevent_close"] =
			[this]()
			{
				m_window->prevent_close();
			};

		window["set_clear_color"] =
			[this](float r, float g, float b, float a)
			{
				m_window->set_clear_color({ r, g, b, a });
			};

		auto on_ui_table{ state.create_table() };
		on_ui_table["subscribe"] =
			[this](std::function<void(double delta)> f) -> size_t
			{
				return m_window->get_ui_event_handler().subscribe([f](const ax::WindowUIEvent& e) { f(e.delta); });
			};
		on_ui_table["unsubscribe"] =
			[this](size_t id)
			{
				m_window->get_ui_event_handler().unsubscribe(id);
			};
		window["on_ui"] = on_ui_table;

		auto on_render_table{ state.create_table() };
		on_render_table["subscribe"] =
			[this](std::function<void(double delta, std::uintptr_t commandBuffer)> f) -> size_t
			{
				return m_window->get_render_event_handler().subscribe([f](const ax::WindowRenderEvent& e) { f(e.delta, reinterpret_cast<std::uintptr_t>(e.commandBuffer)); });
			};
		on_render_table["unsubscribe"] =
			[this](size_t id)
			{
				m_window->get_render_event_handler().unsubscribe(id);
			};
		window["on_render"] = on_render_table;

		auto on_close_table{ state.create_table() };
		on_close_table["subscribe"] =
			[this](std::function<void()> f) -> size_t
			{
				return m_window->get_request_close_event_handler().subscribe([f](const ax::WindowRequestCloseEvent&) { f(); });
			};
		on_close_table["unsubscribe"] =
			[this](size_t id)
			{
				m_window->get_ui_event_handler().unsubscribe(id);
			};
		window["on_close"] = on_close_table;

		window["render"] =
			sol::overload
			(
				[this](const sol::table& texture, float x, float y)
				{
					if (texture["valid"])
						m_window->render_texture(texture["ptr"].get<Texture*>(), { x, y });
					else
						spdlog::error("Invalid texture, cannot render");
				},
				[this](const sol::table& texture, float x, float y, float ax, float ay, float aw, float ah)
				{
					if (texture["valid"])
					{
						const auto t{ texture["ptr"].get<ax::Texture*>() };
						m_window->render_texture(t, { x, y }, { ax, ay, aw, ah });
					}
					else
					{
						spdlog::error("Invalid texture, cannot render");
					}
				},
				[this](const sol::table& texture, float x, float y, float ax, float ay, float aw, float ah, 
					   float r, float z, float cr, float cg, float cb, float ca, float sx, float sy)
				{
					if (texture["valid"])
					{
						const auto t{ texture["ptr"].get<ax::Texture*>() };
						m_window->render_texture(t, { x, y }, r, { ax, ay, aw, ah }, z, { cr, cg, cb, ca }, { sx, sy });
					}
					else
					{
						spdlog::error("Invalid texture, cannot render");
					}
				}
			);

		{
			auto sprite_table{ state.create_table() };
			sprite_table["allocate"] =
				[this]()
				{
					return m_window->allocate_sprite();
				};

			sprite_table["free"] =
				[this](SpriteDefinition* sprite)
				{
					m_window->free_sprite(sprite);
				};

			sprite_table["setup"] =
				sol::overload
				(
					[this](SpriteDefinition* sprite, const sol::table& texture, float x, float y, float rx, float ry, float rw, float rh)
					{
						if (texture["valid"])
						{
							sprite->tex = texture["ptr"].get<Texture*>();
							sprite->gpuData.pos.x = x;
							sprite->gpuData.pos.y = y;
							sprite->gpuData.useRegion = 1;
							sprite->gpuData.region.x = rx;
							sprite->gpuData.region.y = ry;
							sprite->gpuData.region.z = rw;
							sprite->gpuData.region.w = rh;
						}
						else
						{
							spdlog::error("Invalid texture, cannot setup sprite");
						}
					},
					[this](SpriteDefinition* sprite, const sol::table& texture, float x, float y)
					{
						if (texture["valid"])
						{
							sprite->tex = texture["ptr"].get<Texture*>();
							sprite->gpuData.pos.x = x;
							sprite->gpuData.pos.y = y;
							sprite->gpuData.useRegion = 0;
						}
						else
						{
							spdlog::error("Invalid texture, cannot setup sprite");
						}
					}
				);

			sprite_table["update_position"] =
				sol::overload
				(
					[this](SpriteDefinition* sprite, float x, float y)
					{
						sprite->gpuData.pos.x = x;
						sprite->gpuData.pos.y = y;
					},
					[this](SpriteDefinition* sprite, float x, float y, float r)
					{
						sprite->gpuData.pos.x = x;
						sprite->gpuData.pos.y = y;
						sprite->gpuData.rotation = r;
					}
				);

			app["sprites"] = sprite_table;
		}
	}

	auto vec_mult_overloads = sol::overload(
		[](const glm::vec2& a, const glm::vec2& b) -> glm::vec2 { return a * b; },
		[](const glm::vec2& a, float b) -> glm::vec2 { return a * b; },
		[](float a, const glm::vec2& b) -> glm::vec2 { return a * b; }
	);

	auto vec_add_overloads = sol::overload(
		[](const glm::vec2& a, const glm::vec2& b) -> glm::vec2 { return a + b; },
		[](const glm::vec2& a, float b) -> glm::vec2 { return a + b; },
		[](float a, const glm::vec2& b) -> glm::vec2 { return a + b; }
	);

	auto vec_sub_overloads = sol::overload(
		[](const glm::vec2& a, const glm::vec2& b) -> glm::vec2 { return a - b; },
		[](const glm::vec2& a, float b) -> glm::vec2 { return a - b; },
		[](float a, const glm::vec2& b) -> glm::vec2 { return a - b; }
	);

	auto vec_div_overloads = sol::overload(
		[](const glm::vec2& a, const glm::vec2& b) -> glm::vec2 { return a / b; },
		[](const glm::vec2& a, float b) -> glm::vec2 { return a / b; },
		[](float a, const glm::vec2& b) -> glm::vec2 { return a / b; }
	);

	state.new_usertype<glm::vec2>
		(
			"vec2",

			"new", sol::constructors<void(float, float)>(),
			"x", & glm::vec2::x,
			"y", & glm::vec2::y,
			"normalize", [](const glm::vec2& in) { return glm::normalize(in); },
			"length", [](const glm::vec2& in) { return glm::length(in); },
			sol::meta_function::multiplication, vec_mult_overloads,
			sol::meta_function::addition, vec_add_overloads,
			sol::meta_function::subtraction, vec_sub_overloads,
			sol::meta_function::division, vec_div_overloads,
			sol::meta_function::to_string, [](const glm::vec2& in) { return glm::to_string(in); }
		);

	auto vec4_mult_overloads = sol::overload(
		[](const glm::vec4& a, const glm::vec4& b) -> glm::vec4 { return a * b; },
		[](const glm::vec4& a, float b) -> glm::vec4 { return a * b; },
		[](float a, const glm::vec4& b) -> glm::vec4 { return a * b; }
	);

	auto vec4_add_overloads = sol::overload(
		[](const glm::vec4& a, const glm::vec4& b) -> glm::vec4 { return a + b; },
		[](const glm::vec4& a, float b) -> glm::vec4 { return a + b; },
		[](float a, const glm::vec4& b) -> glm::vec4 { return a + b; }
	);

	auto vec4_sub_overloads = sol::overload(
		[](const glm::vec4& a, const glm::vec4& b) -> glm::vec4 { return a - b; },
		[](const glm::vec4& a, float b) -> glm::vec4 { return a - b; },
		[](float a, const glm::vec4& b) -> glm::vec4 { return a - b; }
	);

	auto vec4_div_overloads = sol::overload(
		[](const glm::vec4& a, const glm::vec4& b) -> glm::vec4 { return a / b; },
		[](const glm::vec4& a, float b) -> glm::vec4 { return a / b; },
		[](float a, const glm::vec4& b) -> glm::vec4 { return a / b; }
	);

	state.new_usertype<glm::vec4>
		(
			"vec4",

			"new", sol::constructors<void(float, float, float, float)>(),
			"x", & glm::vec4::x,
			"y", & glm::vec4::y,
			"z", & glm::vec4::z,
			"w", & glm::vec4::w,
			"normalize", [](const glm::vec4& in) { return glm::normalize(in); },
			sol::meta_function::multiplication, vec4_mult_overloads,
			sol::meta_function::addition, vec4_add_overloads,
			sol::meta_function::subtraction, vec4_sub_overloads,
			sol::meta_function::division, vec4_div_overloads,
			sol::meta_function::to_string, [](const glm::vec4& in) { return glm::to_string(in); }
		);

	state.new_usertype<SpriteDefinition>
		(
			"sprite",
			"pos", sol::property(
				[](SpriteDefinition& sprite) -> glm::vec2& { return sprite.gpuData.pos; },
				[](SpriteDefinition& sprite, const glm::vec2& pos) { sprite.gpuData.pos = pos; }
			),
			"region", sol::property(
				[](SpriteDefinition& sprite) -> rectf& { return sprite.gpuData.region; },
				[](SpriteDefinition& sprite, const rectf& region) { sprite.gpuData.region = region; }
			),
			"use_region", sol::property(
				[](const SpriteDefinition& sprite) { return sprite.gpuData.useRegion != 0; },
				[](SpriteDefinition& sprite, bool useRegion) { sprite.gpuData.useRegion = useRegion ? 1u : 0u; }
			),
			"z", sol::property(
				[](SpriteDefinition& sprite) -> float& { return sprite.gpuData.z; },
				[](SpriteDefinition& sprite, float z) { sprite.gpuData.z = z; }
			),
			"scale", sol::property(
				[](SpriteDefinition& sprite) -> glm::vec2& { return sprite.gpuData.scale; },
				[](SpriteDefinition& sprite, const glm::vec2& scale) { sprite.gpuData.scale = scale; }
			),
			"rotation", sol::property(
				[](SpriteDefinition& sprite) -> float& { return sprite.gpuData.rotation; },
				[](SpriteDefinition& sprite, float rotation) { sprite.gpuData.rotation = rotation; }
			),
			"tint", sol::property(
				[](SpriteDefinition& sprite) -> glm::vec4& { return sprite.gpuData.tint; },
				[](SpriteDefinition& sprite, const glm::vec4& tint) { sprite.gpuData.tint = tint; }
			)
		);
}

void ax::Application::add_manifest_bindings(sol::state&)
{
}

bool ax::Application::try_load(const std::vector<std::string>& args)
{
	LogTimer _timer{ "Application Load" };

	ax::lua::libs::register_embedded();
	ax::Resource::setup_loader(m_loader);
	m_scripts.setup({});

	ax::lua::Script* manifest{ m_scripts.load("!manifest", "manifest.luac") };
	m_env = m_scripts.create_env();

	m_env["args"] = args;

	add_manifest_bindings(m_scripts.state({}));

	if (!manifest) 
	{
		spdlog::error("Failed to load manifest");
		return false;
	}

	const auto& res{ manifest->run(m_env) };

	if (!res.valid())
	{
		const sol::error msg = res;
		spdlog::error("Failed to parse manifest: {}", msg.what());
		return false;
	}

	ax::lua::Script* stdlib{ m_scripts.load("@std", "@std")};
	const auto& std_res{ stdlib->run(m_env) };
	if (!std_res.valid())
	{
		const sol::error msg = std_res;
		spdlog::error("Failed to load @std lib: {}", msg.what());
		return false;
	}

	const auto& app{ m_env["app"] };
	m_name = app["name"];
	
	const auto& headless_res{ app["headless"] };
	if (headless_res.valid())
		m_create_window = !headless_res.get<bool>();

	const auto& window{ app["window"] };
	if (m_create_window)
	{
		init_window();
		window["handle"] = (void*)m_window->glfw_handle();

		const sol::table& textures{ app["textures"].get<sol::table>() };
		for (const auto& entry : textures)
			m_textures.load(entry.first.as<std::string>(), entry.second.as<std::string>());
	}

	if (m_create_window)
	{
		const auto& icon{ m_textures.get(window["icon"])->create_glfw_image() };
		glfwSetWindowIcon(m_window->glfw_handle(), 1, &icon);
	}

	const sol::table& scripts{ app["scripts"].get<sol::table>() };
	for (const auto& entry : scripts)
		m_scripts.load(entry.first.as<std::string>(), entry.second.as<std::string>());

	//const sol::table& types{ app["types"].get<sol::table>() };
	//for (const auto& entry : types)
	//	m_typeGen.register_type(entry.first.as<std::string>(), entry.second.as<ax::type::TypeDef>());

	std::string entryPointScript = app["entry_point"];
	m_entryPoint = m_scripts.get(entryPointScript);

	if (!m_entryPoint)
	{
		spdlog::error("Failed to load entry point script: '{}'", entryPointScript);
		return false;
	}

	add_application_bindings(m_scripts.state({}));

	spdlog::info("<Lua> Running entry point script: '{}'", entryPointScript);
	const auto ep_res = m_entryPoint->run(m_env);
	if (!ep_res.valid())
	{
		const sol::error msg = ep_res;
		spdlog::error("Failed to run entry point script: {}", msg.what());
		return false;
	}
	else if(const auto err = ep_res.get<ax::Error>(); err != ax::Error::Success)
	{
		spdlog::error("Entry point script failed: {}", ax::get_error_name(err));
		return false;
	}

	m_loaded = true;
	return m_loaded;
}

void ax::Application::cleanup()
{
	m_scripts.cleanup({});
	m_textures.unload_all(Badge<Application>{});

	if (m_window)
		m_window = nullptr;

	ax::Resource::cleanup_loader(m_loader);
}

ax::Application::~Application()
{
	if (m_loaded)
		cleanup();

	m_scripts.cleanup({});
}

ax::Application::Application(ResourceLoader&& loader)
	: m_loader{ std::move(loader) }
	, m_scripts{ {}, m_loader }
	, m_textures{ Badge<Application>{}, m_loader }
{
}
