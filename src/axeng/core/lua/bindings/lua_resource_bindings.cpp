#include "axeng/core/lua/bindings/lua_resource_bindings.h"
#include "axeng/core/application.h"
#include "axeng/core/font_sheet.h"
#include "axeng/core/lua/bindings/lua_texture_bindings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

void ax::lua::bindings::setup_resource_bindings(ax::Application& app, sol::state& state)
{
	auto app_table{ state["app"].get<sol::table>() };
	{
		auto resource_lookup_table{ state.create_table() };
		const auto addFontMetrics = [&app, &state](sol::table& texture)
		{
			auto glyphs{ state.create_table() };
			const auto setBitmap = [](sol::table& table, const FontGlyphBitmap& bitmap)
			{
				table["x"] = bitmap.x;
				table["y"] = bitmap.y;
				table["width"] = bitmap.width;
				table["height"] = bitmap.height;
				table["x_offset"] = bitmap.xOffset;
				table["y_offset"] = bitmap.yOffset;
			};

			for (size_t index{}; index < font_glyph_count; ++index)
			{
				const auto& glyph{ app.m_fontSheet.glyphs[index] };
				auto glyphTable{ state.create_table() };
				auto regular{ state.create_table() };
				auto bold{ state.create_table() };
				setBitmap(regular, glyph.regular);
				setBitmap(bold, glyph.bold);
				glyphTable["regular"] = regular;
				glyphTable["bold"] = bold;
				glyphTable["advance"] = glyph.advance;
				glyphs[static_cast<int>(font_first_codepoint + index)] = glyphTable;
			}

			texture["glyphs"] = glyphs;
			texture["baseline"] = app.m_fontSheet.baseline;
			texture["line_height"] = app.m_fontSheet.lineHeight;
			texture["space_advance"] = app.m_fontSheet.glyphs[0].advance;
		};

		if (app.m_atlasTexture == nullptr)
		{
			resource_lookup_table["get_texture"] =
				[&app, &state, addFontMetrics](const std::string& name) -> sol::table
				{
					const bool isFont{ is_font_texture_name(name) };
					auto text{ app.m_textures.get(isFont ? "__builtin_font" : name) };

					auto ret = state.create_table();

					if (text)
					{
						ret["valid"] = true;
						ret["ptr"] = text;
						ret["view"] = text->view();
						ret["ui_view"] = text->imgui_view();
						ret["width"] = text->width();
						ret["height"] = text->height();
						if (isFont)
							addFontMetrics(ret);
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
				[&app, &state, addFontMetrics](const std::string& name) -> sol::table
				{
					const bool isFont{ is_font_texture_name(name) };
					auto text{ app.m_atlasTexture };
					auto region{ text->region(isFont ? "__builtin_font" : name) };
					if (isFont && !region.has_value())
					{
						text = app.m_textures.get("__builtin_font");
						if (text)
							region = rectf{ 0.0f, 0.0f, static_cast<float>(text->width()), static_cast<float>(text->height()) };
					}

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
						if (isFont)
							addFontMetrics(ret);
					}
					else
					{
						ret["valid"] = false;
					}

					return ret;
				};
		}

		const auto make_temp_texture_table =
			[&state](const std::string& name, const std::shared_ptr<LuaTextureLease>& lease) -> sol::table
			{
				auto ret = state.create_table();
				if (!lease)
				{
					ret["valid"] = false;
					return ret;
				}

				const auto& text{ lease->texture };
				ret["temp"] = true;
				ret["name"] = name;
				ret["view"] = text->view();
				ret["ui_view"] = text->imgui_view();
				ret["width"] = text->width();
				ret["height"] = text->height();
				ret["_lease"] = lease;
				auto metatable{ state.create_table() };
				metatable["__index"] =
					[lease](sol::table table, sol::object key) -> sol::object
					{
						if (key.get_type() != sol::type::string)
							return sol::make_object(table.lua_state(), sol::nil);
						const auto field{ key.as<std::string>() };
						if (field == "valid")
							return sol::make_object(table.lua_state(), lease->active.load(std::memory_order_relaxed));
						if (field == "ptr")
						{
							if (lease->active.load(std::memory_order_relaxed))
								return sol::make_object(table.lua_state(), lease->texture.get());
							return sol::make_object(table.lua_state(), sol::nil);
						}
						return sol::make_object(table.lua_state(), sol::nil);
					};
				ret[sol::metatable_key] = metatable;
				return ret;
			};

		resource_lookup_table["get_sdf_font"] =
			[&app, &state]() -> sol::table
			{
				auto ret{ state.create_table() };
				auto* const text{ app.m_textures.get(std::string{ sdf_font_texture_name }) };
				if (text == nullptr)
				{
					ret["valid"] = false;
					return ret;
				}

				const auto& font{ app.m_sdfFont };
				auto glyphs{ state.create_table() };
				for (size_t index{}; index < font_glyph_count; ++index)
				{
					const auto& glyph{ font.glyphs[index] };
					glyphs[static_cast<int>(font_first_codepoint + index)] = state.create_table_with(
						"x", glyph.x, "y", glyph.y, "width", glyph.width, "height", glyph.height,
						"x_offset", glyph.xOffset, "y_offset", glyph.yOffset, "advance", glyph.advance);
				}
				ret["valid"] = true;
				ret["sdf"] = true;
				ret["ptr"] = text;
				ret["view"] = text->view();
				ret["ui_view"] = text->imgui_view();
				ret["width"] = text->width();
				ret["height"] = text->height();
				ret["glyphs"] = glyphs;
				ret["baseline"] = font.baseline;
				ret["line_height"] = font.lineHeight;
				ret["space_advance"] = font.glyphs[0].advance;
				ret["texel_scale"] = font.texelScale;
				// Glyph rectangles include the distance field's spread on every side, in reference pixels.
				ret["glyph_padding"] = static_cast<float>(sdf_font_spread) * font.texelScale;
				return ret;
			};

		resource_lookup_table["create_temp_texture"] =
			[&app, make_temp_texture_table](const std::string& name, const float width, const float height) -> sol::table
			{
				std::lock_guard lock{ app.m_tempTexturesMutex };
				auto it{ app.m_tempTextures.find(name) };
				if (it == app.m_tempTextures.end())
				{
					auto texture{ std::shared_ptr<Texture>{ app.m_textures.create_temporary(name, {}, width, height) } };
					if (texture)
					{
						auto lease{ std::make_shared<LuaTextureLease>(std::move(texture)) };
						it = app.m_tempTextures.emplace(name, std::move(lease)).first;
					}
				}

				return make_temp_texture_table(name, it != app.m_tempTextures.end() ? it->second : std::shared_ptr<LuaTextureLease>{});
			};

		resource_lookup_table["create_temp_texture_from_image"] =
			[&app, make_temp_texture_table]
			(const std::string& name, const std::string& data, sol::optional<float> maxWidth, sol::optional<float> maxHeight) -> sol::table
			{
				const auto limit{ [](const sol::optional<float>& value) -> uint32_t
				{
					if (!value || !std::isfinite(*value) || *value < 1.0f)
						return 0;
					return static_cast<uint32_t>(std::min(*value, static_cast<float>(std::numeric_limits<uint32_t>::max())));
				} };

				std::lock_guard lock{ app.m_tempTexturesMutex };
				auto it{ app.m_tempTextures.find(name) };
				if (it == app.m_tempTextures.end())
				{
					auto texture{ std::shared_ptr<Texture>{ app.m_textures.create_temporary_from_image(
						name, std::vector<uint8_t>{ data.begin(), data.end() }, limit(maxWidth), limit(maxHeight)) } };
					if (texture)
					{
						auto lease{ std::make_shared<LuaTextureLease>(std::move(texture)) };
						it = app.m_tempTextures.emplace(name, std::move(lease)).first;
					}
				}

				return make_temp_texture_table(name, it != app.m_tempTextures.end() ? it->second : std::shared_ptr<LuaTextureLease>{});
			};

		resource_lookup_table["release_temp_texture"] =
			[&app](sol::table tbl) -> bool
			{
				if (!tbl["temp"].get_or(false))
				{
					spdlog::warn("Attempted to release a non-temporary texture.");
					return false;
				}
				if (!tbl["valid"])
				{
					spdlog::warn("Attempted to release an invalid temporary texture.");
					return true;
				}
				std::lock_guard lock{ app.m_tempTexturesMutex };
				if (auto it = app.m_tempTextures.find(tbl["name"]); it != app.m_tempTextures.end())
				{
					if (app.window())
						app.window()->invalidate_texture_bind_group(it->second->texture.get());
					it->second->active.store(false, std::memory_order_relaxed);
					app.m_tempTextures.erase(it);
					return true;
				}

				spdlog::warn("Attempted to release a temporary texture that does not exist (don't release asset-based textures)");
				return false;
			};

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

		if (app.has_permission(ax::lua::Permission::IO))
		{
			resource_lookup_table["save_texture_io"] =
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

		resource_lookup_table["save_texture_user"] =
			[&app](const std::string& name, const std::string& path) -> bool
			{
				auto outPath{ app.user_files().get_user_file_path(path) };
				auto text{ app.m_textures.get(name) };
				if (text)
				{
					return outPath.has_value() && text->save_png(outPath->string()) == ax::Error::Success;
				}
				return false;
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
