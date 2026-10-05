#include "axeng/core/lua/bindings/lua_application_bindings.h"

#include "axeng/core/application.h"
#include "axeng/core/event.h"
#include "axeng/core/lua/bindings/lua_texture_bindings.h"
#include "axeng/core/lua/bindings/lua_window_bindings.h"
#include "spdlog/spdlog.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/string_cast.hpp"

void ax::lua::bindings::setup_application_bindings(ax::Application& app, sol::state& state)
{
	auto app_table{ state.create_table() };

	auto user_io_table{ state.create_table() };
	user_io_table["open"] =
		[&app](const std::string& filename, const char* mode) -> UserFileHandle*
		{
			return app.m_userFileManager.open_file(filename, mode);
		};
	app_table["user_io"] = user_io_table;

	state.new_usertype<UserFileHandle>
	(
		"UserFileHandle",
		"read_all", &ax::UserFileHandle::read_all,
		"write", &ax::UserFileHandle::write,
		"close", &ax::UserFileHandle::close
	);

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

	app_table["get_or_create_shared"] =
		[&app](const std::string& name) -> ax::lua::SharedObject*
		{
			std::lock_guard lock{ app.m_shared_mutex };
			if (auto it = app.m_shared.find(name); it != app.m_shared.end())
			{
				it->second.increment_ref_count();
				return &it->second;
			}
			auto it{ app.m_shared.try_emplace(name).first };
			it->second.increment_ref_count();
			return &it->second;
		};

	app_table["release_shared"] =
		[&app](ax::lua::SharedObject* so)
		{
			std::lock_guard lock{ app.m_shared_mutex };
			const auto count{ so->decrement_ref_count() };
			if (count == 0)
			{
				for (auto it{ app.m_shared.begin() }; it != app.m_shared.end(); ++it)
				{
					if (&it->second == so)
					{
						app.m_shared.erase(it);
						break;
					}
				}
			}
		};

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

	{
		auto resource_lookup_table{ state.create_table() };

		if (app.m_atlasTexture == nullptr)
		{
			resource_lookup_table["get_texture"] =
				[&app, &state](const std::string& name) -> sol::table
				{
					auto text{ app.m_textures.get(name) };

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
		}
		else
		{
			resource_lookup_table["get_texture"] =
				[&app, &state](const std::string& name) -> sol::table
				{
					auto text{ app.m_atlasTexture };
					auto region{ app.m_atlasTexture->region(name) };

					auto ret = state.create_table();

					if (region.has_value())
					{
						ret["valid"] = true;
						ret["ptr"] = text;
						ret["view"] = text->view();
						ret["ui_view"] = text->imgui_view();
						ret["width"] = region->z;
						ret["height"] = region->w;
						ret["atlas_region"] = state.create_table();
						ret["atlas_region"][1] = region->x;
						ret["atlas_region"][2] = region->y;
						ret["atlas_region"][3] = region->z;
						ret["atlas_region"][4] = region->w;
					}
					else
					{
						ret["valid"] = false;
					}

					return ret;
				};
		}

		resource_lookup_table["create_texture"] =
			[&app, &state](const std::string& name, const std::string& data) -> sol::table
			{
				auto text{ app.m_textures.get(name) };

				if (text == nullptr)
					text = app.m_textures.load_from_raw(name, {}, std::vector<uint8_t>{ data.begin(), data.end() });

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

		resource_lookup_table["get_texture_regions"] =
			[&app, &state](const std::string& name) -> sol::table
			{
				auto text{ app.m_textures.get(name) };

				auto ret = state.create_table();

				if (text)
				{
					ret["valid"] = true;
					ret["regions"] = state.create_table();
					for (const auto& kvp : text->regions())
					{
						auto tbl{ state.create_table() };
						tbl[1] = kvp.second.x;
						tbl[2] = kvp.second.y;
						tbl[3] = kvp.second.z;
						tbl[4] = kvp.second.w;
						ret["regions"][kvp.first] = tbl;
					}
				}
				else
				{
					ret["valid"] = false;
				}

				return ret;
			};

		if (app.m_allowedPermissions[ax::lua::Permission::IO])
		{
			resource_lookup_table["create_mega_texture"] =
				[&app](const std::string& name, const sol::table& textureDescs, float minPadding) -> ax::Error
				{
					std::vector<ax::Texture::Descriptor> descs{};
					for (const auto& kvp : textureDescs)
					{
						descs.push_back(kvp.second.as<std::string>());
					}

					const auto err{ app.m_textures.create_texture_atlas(name, descs, minPadding) };
					return err;
				};

			resource_lookup_table["save_texture"] =
				[&app](const std::string& name, const std::string& path) -> bool
				{
					auto text{ app.m_textures.get(name) };
					if (text)
					{
						return text->save_png(path) == ax::Error::Success;
					}
					return false;
				};
		}

		resource_lookup_table["get_script"] =
			[&app, &state](const std::string& name) -> sol::table
			{
				auto script{ app.m_scripts.get(name) };

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

		app_table["res"] = resource_lookup_table;
	}
	{
		auto sprite_table{ state.create_table() };
		sprite_table["allocate"] =
			[&app]()
			{
				return app.m_window->allocate_sprite();
			};

		sprite_table["free"] =
			[&app](SpriteDefinition* sprite)
			{
				app.m_window->free_sprite(sprite);
			};

		sprite_table["setup"] =
			sol::overload
			(
				[useAtlas = app.m_atlasTexture != nullptr](SpriteDefinition* sprite, const sol::table& texture, float x, float y, float rx, float ry, float rw, float rh)
				{
					if (texture["valid"])
					{
						sprite->tex = texture["ptr"].get<Texture*>();
						sprite->gpuData.pos.x = x;
						sprite->gpuData.pos.y = y;
						sprite->gpuData.useRegion = 1;
						sprite->gpuData.region = useAtlas
							? get_texture_region(texture, { rx, ry, rw, rh })
							: rectf{ rx, ry, rw, rh };
					}
					else
					{
						spdlog::error("Invalid texture, cannot setup sprite");
					}
				},
				[useAtlas = app.m_atlasTexture != nullptr](SpriteDefinition* sprite, const sol::table& texture, float x, float y)
				{
					if (texture["valid"])
					{
						sprite->tex = texture["ptr"].get<Texture*>();
						sprite->gpuData.pos.x = x;
						sprite->gpuData.pos.y = y;
						sprite->gpuData.useRegion = useAtlas ? 1 : 0;
						if (useAtlas)
							sprite->gpuData.region = get_texture_region(texture);
					}
					else
					{
						spdlog::error("Invalid texture, cannot setup sprite");
					}
				}
			);

		sprite_table["update_position"] =
			[](SpriteDefinition* sprite, float x, float y, float z, float r)
			{
				sprite->gpuData.pos.x = x;
				sprite->gpuData.pos.y = y;
				sprite->gpuData.z = z;
				sprite->gpuData.rotation = r;
			};

		sprite_table["update_positions"] =
			[](const sol::table& updates)
			{
				for (std::size_t i = 1; i <= updates.size(); ++i)
				{
					const sol::table& update{ updates.get<sol::table>(i) };
					SpriteDefinition* sprite{ update.get<SpriteDefinition*>("sprite") };
					sprite->gpuData.pos.x = update.get<float>("x");
					sprite->gpuData.pos.y = update.get<float>("y");
					sprite->gpuData.rotation = update.get<float>("r");
				}
			};

		app_table["sprites"] = sprite_table;
	}

	auto vec_mult_overloads = 
		sol::overload
		(
			[](const glm::vec2& a, const glm::vec2& b) -> glm::vec2 { return a * b; },
			[](const glm::vec2& a, float b) -> glm::vec2 { return a * b; },
			[](float a, const glm::vec2& b) -> glm::vec2 { return a * b; }
		);

	auto vec_add_overloads = 
		sol::overload
		(
			[](const glm::vec2& a, const glm::vec2& b) -> glm::vec2 { return a + b; },
			[](const glm::vec2& a, float b) -> glm::vec2 { return a + b; },
			[](float a, const glm::vec2& b) -> glm::vec2 { return a + b; }
		);

	auto vec_sub_overloads =
		sol::overload
		(
			[](const glm::vec2& a, const glm::vec2& b) -> glm::vec2 { return a - b; },
			[](const glm::vec2& a, float b) -> glm::vec2 { return a - b; },
			[](float a, const glm::vec2& b) -> glm::vec2 { return a - b; }
		);

	auto vec_div_overloads = 
		sol::overload
		(
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

	auto vec4_mult_overloads = 
		sol::overload
		(
			[](const glm::vec4& a, const glm::vec4& b) -> glm::vec4 { return a * b; },
			[](const glm::vec4& a, float b) -> glm::vec4 { return a * b; },
			[](float a, const glm::vec4& b) -> glm::vec4 { return a * b; }
		);

	auto vec4_add_overloads = 
		sol::overload
		(
			[](const glm::vec4& a, const glm::vec4& b) -> glm::vec4 { return a + b; },
			[](const glm::vec4& a, float b) -> glm::vec4 { return a + b; },
			[](float a, const glm::vec4& b) -> glm::vec4 { return a + b; }
		);

	auto vec4_sub_overloads = 
		sol::overload
		(
			[](const glm::vec4& a, const glm::vec4& b) -> glm::vec4 { return a - b; },
			[](const glm::vec4& a, float b) -> glm::vec4 { return a - b; },
			[](float a, const glm::vec4& b) -> glm::vec4 { return a - b; }
		);

	auto vec4_div_overloads = 
		sol::overload
		(
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

	state["app"] = app_table;

	bind_conditional_to_state(app, state);

	for (const auto& entry : app.m_conditionalBindings)
	{
		const auto& binding{ entry.second };
		if (binding.first(app, state))
			binding.second(app, state);
	}
}
