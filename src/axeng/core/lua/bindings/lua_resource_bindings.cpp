#include "axeng/core/lua/bindings/lua_resource_bindings.h"
#include "axeng/core/application.h"
#include "axeng/core/lua/bindings/lua_texture_bindings.h"

void ax::lua::bindings::setup_resource_bindings(ax::Application& app, sol::state& state)
{
	auto app_table{ state["app"].get<sol::table>() };
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
}
