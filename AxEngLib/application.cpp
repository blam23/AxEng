#include "application.h"

#include "lua_lib_loader.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/string_cast.hpp"

ax::Application ax::Application::from_directory(flag_set<lua::Permission> permissions, std::string_view root)
{
	return { permissions, DirectoryResourceLoader{ root } };
}

ax::Application ax::Application::from_embedded(flag_set<lua::Permission> permissions, EmbeddedResourceLayout&& layout)
{
	return { permissions, std::move(layout) };
}

ax::Application ax::Application::from_zip(flag_set<lua::Permission> permissions, std::string_view zipFile)
{
	return { permissions, ZipResourceLoader{ zipFile } };
}

bool ax::Application::init_window()
{
	LogTimer _timer{ "wgpu initial setup" };

	const auto& manifest{ m_env["manifest"] };
	const auto& window{ manifest["window"] };
	m_window = std::make_unique<Window>(WindowDefinition
	{
		.width = window["width"],
		.height = window["height"],
		.title = window["title"],
		.vsync = window["vsync"],
	});

	bool success{ true };
	success = m_window->init_webgpu();
	if (!success)
		return false;

	m_textures.set_device(m_window->device());

	success = m_window->init_imgui();
	return success;
}

void ax::Application::add_application_bindings(sol::state& state)
{
	auto app{ state.create_table() };

	app["get_or_create_shared"] =
		[this](const std::string& name) -> ax::lua::SharedObject*
		{
			std::lock_guard lock{ m_shared_mutex };
			if (auto it = m_shared.find(name); it != m_shared.end())
			{
				it->second.increment_ref_count();
				return &it->second;
			}
			auto it{ m_shared.try_emplace(name).first };
			it->second.increment_ref_count();
			return &it->second;
		};

	app["release_shared"] =
		[this](ax::lua::SharedObject* so)
		{
			std::lock_guard lock{ m_shared_mutex };
			const auto count{ so->decrement_ref_count() };
			if (count == 0)
			{
				for (auto it{ m_shared.begin() }; it != m_shared.end(); ++it)
				{
					if (&it->second == so)
					{
						m_shared.erase(it);
						break;
					}
				}
			}
		};

	{
		auto on_update_table{ state.create_table() };
		on_update_table["subscribe"] =
			[this](sol::protected_function f) -> size_t
			{
				return m_window->get_update_event_handler().subscribe
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
					ret["view"] = text->view();
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
					ret["view"] = text->view();
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

	if (m_create_window)
	{
		auto window_table{ state.create_table() };
		window_table["handle"] = (void*)m_window->glfw_handle();

		if (m_allowedPermissions[lua::Permission::OS])
		{
			window_table["try_embed_child"] =
				[this](float procIdFloat) -> bool
				{
					DWORD processID = static_cast<DWORD>(procIdFloat);
					return m_window->try_embed_child(processID);
				};

			window_table["set_embedded_child_position"] =
				[this](float procIdFloat, float x, float y, float width, float height)
				{
					DWORD processID = static_cast<DWORD>(procIdFloat);
					m_window->set_embedded_child_position(processID, static_cast<int>(x), static_cast<int>(y), static_cast<int>(width), static_cast<int>(height));
				};

			window_table["try_kill_child"] =
				[this](float procIdFloat)
				{
					DWORD processID = static_cast<DWORD>(procIdFloat);
					m_window->try_kill_child(processID);
				};

			window_table["focus_child"] =
				[this](float procIdFloat)
				{
					DWORD processID = static_cast<DWORD>(procIdFloat);
					return m_window->focus_child(processID);
				};

			window_table["redirect_input_to_child"] =
				[this](float procIdFloat)
				{
					DWORD processID = static_cast<DWORD>(procIdFloat);
					m_window->redirect_input_to_child(processID);
				};

			window_table["reset_input_redirection"] =
				[this]()
				{
					m_window->reset_input_redirection();
				};
		}

		window_table["prevent_close"] =
			[this]()
			{
				m_window->prevent_close();
			};

		window_table["set_clear_color"] =
			[this](float r, float g, float b, float a)
			{
				m_window->set_clear_color({ r, g, b, a });
			};

		auto on_ui_table{ state.create_table() };
		on_ui_table["subscribe"] =
			[this](sol::protected_function f) -> size_t
			{
				return m_window->get_ui_event_handler().subscribe([f](const ax::WindowUIEvent& e)
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
			[this](size_t id)
			{
				m_window->get_ui_event_handler().unsubscribe(id);
			};
		window_table["on_ui"] = on_ui_table;

		auto on_render_table{ state.create_table() };
		on_render_table["subscribe"] =
			[this](sol::protected_function f) -> size_t
			{
				return m_window->get_render_event_handler().subscribe([f](const ax::WindowRenderEvent& e)
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
			[this](size_t id)
			{
				m_window->get_render_event_handler().unsubscribe(id);
			};
		window_table["on_render"] = on_render_table;

		auto on_close_table{ state.create_table() };
		on_close_table["subscribe"] =
			[this](sol::protected_function f) -> size_t
			{
				return m_window->get_request_close_event_handler().subscribe([f](const ax::WindowRequestCloseEvent&) {
					const auto res{ f() };
					if (!res.valid())
					{
						const sol::error err = res;
						spdlog::error("Failed to run on_close callback: {}", err.what());
					}
				});
			};
		on_close_table["unsubscribe"] =
			[this](size_t id)
			{
				m_window->get_request_close_event_handler().unsubscribe(id);
			};
		window_table["on_close"] = on_close_table;

		auto on_resize_table{ state.create_table() };
		on_resize_table["subscribe"] =
			[this](sol::protected_function f) -> size_t
			{
				return m_window->get_resize_event_handler().subscribe([f](const ax::WindowResizeEvent& e)
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
			[this](size_t id)
			{
				m_window->get_resize_event_handler().unsubscribe(id);
			};
		window_table["on_resize"] = on_resize_table;

		m_window->get_resize_event_handler().subscribe([this](const ax::WindowResizeEvent& e)
			{
				const auto& window_table{ m_scripts.state({})["app"]["window"] };
				window_table["width"] = e.width;
				window_table["height"] = e.height;
			});

		window_table["width"] = m_window->width();
		window_table["height"] = m_window->height();

		window_table["render"] =
			sol::overload
			(
				[this](const sol::table& texture, float x, float y)
				{
					if (texture["valid"])
						m_window->render_texture(texture["ptr"].get<Texture*>(), { x, y });
					else
						spdlog::error("Invalid texture, cannot render");
				},
				[this](const sol::table& texture, float x, float y, float z)
				{
					if (texture["valid"])
						m_window->render_texture(texture["ptr"].get<Texture*>(), { x, y }, z);
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
				[this](const sol::table& texture, float x, float y, float ax, float ay, float aw, float ah, float z)
				{
					if (texture["valid"])
					{
						const auto t{ texture["ptr"].get<ax::Texture*>() };
						m_window->render_texture(t, { x, y }, { ax, ay, aw, ah }, z);
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

		app["window"] = window_table;

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

	using EH = ax::EventHandler<sol::object>;

	state.new_usertype<EH>
		(
			"lua_event",
			"subscribe", &EH::subscribe,
			"unsubscribe", &EH::unsubscribe,
			"fire", &EH::fire
		);

	app["current_executable_path"] =
		[]() -> std::string
		{
			char path[MAX_PATH];
			::GetModuleFileNameA(nullptr, path, MAX_PATH);
			return std::string(path);
		};

	state["app"] = app;
}

void ax::Application::add_thread_bindings(sol::state& state, const std::vector<std::string>& args)
{
	initialise_background_worker(args);

	auto background_table{ state.create_table() };

	background_table["run_script"] =
		[this](const std::string& script_name)
		{
			auto script{ m_scripts.get(script_name) };
			if (script == nullptr)
			{
				spdlog::error("Script '{}' not found", script_name);
				return;
			}
			auto task{ m_backgroundWorker.create_script_task(script) };
			m_backgroundWorker.enqueue(task);
		};

	state["bg"] = background_table;
}

void ax::Application::initialise_background_worker(const std::vector<std::string>& args)
{
	m_backgroundWorker.lua().setup();
	add_application_bindings(m_backgroundWorker.lua().state());
	m_backgroundWorker.start(args);
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

	ax::lua::Script* manifest_script{ m_scripts.load("!manifest", "manifest.luac") };
	m_env = m_scripts.create_env();

	m_env["args"] = args;

	add_manifest_bindings(m_scripts.state({}));

	if (!manifest_script)
	{
		spdlog::error("Failed to load manifest");
		return false;
	}

	const auto& res{ manifest_script->run(m_env) };

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

	auto manifest{ m_env["manifest"] };
	m_name = manifest["name"];

	const sol::table& permissions{ manifest["permissions"].get<sol::table>() };
	for (const auto& entry : permissions)
	{
		const auto perm{ entry.second.as<std::string>() };
		const auto perm_enum{ ax::lua::get_perm_from_string(perm) };
		if (!m_allowedPermissions[perm_enum])
		{
			spdlog::error("Manifest requests permission '{}' which is not allowed", perm);
			return false;
		}
	}
	
	const auto& headless_res{ manifest["headless"] };
	if (headless_res.valid())
		m_create_window = !headless_res.get<bool>();

	const auto& window_manifest{ manifest["window"] };
	if (m_create_window)
	{
		init_window();

		const sol::table& textures{ manifest["textures"].get<sol::table>() };
		for (const auto& entry : textures)
			m_textures.load(entry.first.as<std::string>(), entry.second.as<std::string>());
	}

	if (m_create_window)
	{
		const auto& icon{ m_textures.get(window_manifest["icon"])->create_glfw_image() };
		glfwSetWindowIcon(m_window->glfw_handle(), 1, &icon);
	}

	const sol::table& scripts{ manifest["scripts"].get<sol::table>() };
	for (const auto& entry : scripts)
		m_scripts.load(entry.first.as<std::string>(), entry.second.as<std::string>());

	//const sol::table& types{ manifest["types"].get<sol::table>() };
	//for (const auto& entry : types)
	//	m_typeGen.register_type(entry.first.as<std::string>(), entry.second.as<ax::type::TypeDef>());

	std::string entryPointScript = manifest["entry_point"];
	m_entryPoint = m_scripts.get(entryPointScript);

	if (!m_entryPoint)
	{
		spdlog::error("Failed to load entry point script: '{}'", entryPointScript);
		return false;
	}

	add_application_bindings(m_scripts.state({}));

	sol::table app{ m_scripts.state({})["app"].get<sol::table>() };
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

	app["thread"] = "main";
	app["on_main_thread"] =
		[]() -> bool
		{
			return true;
		};

	app["signal"] =
		[this](lua::SharedObject& obj, const std::string& signal_name)
		{
			obj.signal(signal_name);
		};

	app["on_signal"] =
		[this](lua::SharedObject& obj, const std::string& signal_name, sol::protected_function func)
		{
			obj.on_signal(this, signal_name, func);
		};

	m_scripts.state({}).new_usertype<lua::SharedObject>
		(
			"shared",
			"set", &lua::SharedObject::set,
			"get", [this](lua::SharedObject& ref, const std::string& key) { return ref.get(m_scripts.state({}), key); }
		);

	if (m_allowedPermissions[ax::lua::Permission::Threads])
		add_thread_bindings(m_scripts.state({}), args);

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
	{
		std::lock_guard lock{ m_shared_mutex };
		m_shared.clear();
	}

	m_scripts.cleanup({});

	if (m_window)
		m_window = nullptr;

	ax::Resource::cleanup_loader(m_loader);
}

ax::Application::~Application()
{
	m_backgroundWorker.stop();

	if (m_loaded)
		cleanup();

	m_scripts.cleanup({});
}

void ax::Application::call_deferred(std::function<void()> func)
{
	m_window->call_deferred(std::move(func));
}

ax::Application::Application(flag_set<lua::Permission> permissions, ResourceLoader&& loader)
	: m_loader{ std::move(loader) }
	, m_scripts{ Badge<Application>{}, permissions, m_loader }
	, m_textures{ Badge<Application>{}, m_loader }
	, m_allowedPermissions{ permissions }
	, m_backgroundWorker{ true, permissions }
{
}
