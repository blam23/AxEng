#include "axeng/core/lua/bindings/lua_sprite_bindings.h"
#include "axeng/core/application.h"
#include "axeng/core/lua/bindings/lua_texture_bindings.h"
#include "spdlog/spdlog.h"
#include <unordered_map>

namespace
{
	class LuaSprite
	{
	public:
		explicit LuaSprite(ax::SpriteDefinition* sprite) : m_sprite{ sprite }, m_generation{ sprite->generation } {}
		ax::SpriteDefinition& get() const
		{
			if (!m_sprite->allocated || m_sprite->generation != m_generation)
				throw sol::error("Sprite has been freed or its group released");
			return *m_sprite;
		}
	private:
		ax::SpriteDefinition* m_sprite;
		std::uint64_t m_generation;
	};
}

void ax::lua::bindings::setup_sprite_bindings(ax::Application& app, sol::state& state)
{
	auto app_table{ state["app"].get<sol::table>() };
	{
		auto sprite_table{ state.create_table() };
		sprite_table["stats"] = [&app, &state]()
		{
			const auto window{ app.window() };
			return state.create_table_with("sprites", window->get_sprite_count(),
				"allocations", window->sprite_allocations(), "frees", window->sprite_frees(),
				"static_batches", window->static_batch_count(), "static_uploads", window->static_uploads());
		};
		sprite_table["setup_deadline"] = [&app]() { return app.window()->sprite_setup_deadline(); };
		state.new_usertype<StaticSpriteBatch>("static_sprite_batch", sol::no_constructor,
			"set_visible", &StaticSpriteBatch::set_visible,
			"set_unlit", &StaticSpriteBatch::set_unlit, "unlit", &StaticSpriteBatch::unlit,
			"set_staging", &StaticSpriteBatch::set_staging,
			"staging_eligible", &StaticSpriteBatch::staging_eligible,
			"staging_priority", &StaticSpriteBatch::staging_priority,
			"visible", &StaticSpriteBatch::visible, "ready", &StaticSpriteBatch::ready,
			"error", &StaticSpriteBatch::error,
			"release", &StaticSpriteBatch::release, "size", &StaticSpriteBatch::size);
		state.new_usertype<SpriteGroup>("sprite_group", sol::no_constructor,
			"set_visible", &SpriteGroup::set_visible, "visible", &SpriteGroup::visible,
			"size", &SpriteGroup::size,
			"release", [&app](std::shared_ptr<SpriteGroup> group) { app.window()->release_sprite_group(group); });
		sprite_table["group"] = [&app]() { return app.window()->create_sprite_group(); };
		sprite_table["transfer"] = [&app](const LuaSprite& sprite, std::shared_ptr<SpriteGroup> group)
		{
			app.window()->transfer_sprite(&sprite.get(), group);
		};
		sprite_table["attach_batch"] = [&app](const std::string& name, std::shared_ptr<SpriteBuffer> data, std::int64_t order)
		{
			Texture* texture{ app.m_atlasTexture ? app.m_atlasTexture : app.textures().get(name) };
			glm::vec2 offset{};
			if (app.m_atlasTexture)
			{
				const auto region{ texture->region(name) };
				if (!region)
					throw sol::error("Unknown atlas texture: " + name);
				offset = { region->x, region->y };
			}
			return app.window()->attach_sprite_batch(texture, std::move(data), offset, order);
		};
		sprite_table["allocate"] =
			sol::overload(
				[&app]() { return LuaSprite{ app.m_window->allocate_sprite() }; },
				[&app](std::shared_ptr<SpriteGroup> group) { return LuaSprite{ app.m_window->allocate_group_sprite(group) }; });

		sprite_table["free"] =
			[&app](const LuaSprite& sprite)
			{
				app.m_window->free_sprite(&sprite.get());
			};

		sprite_table["setup"] =
			[useAtlas = app.m_atlasTexture != nullptr]
			(
				const LuaSprite& handle,
				const sol::table& texture,
				float x,
				float y,
				sol::variadic_args args
			)
			{
				auto* const sprite{ &handle.get() };
				if (!texture["valid"])
				{
					spdlog::error("Invalid texture, cannot setup sprite");
					return;
				}

				auto keepAlive{ get_texture_lease(texture) };
				if (texture["temp"].get_or(false) && !keepAlive)
				{
					spdlog::error("Invalid temporary texture, cannot setup sprite");
					return;
				}

				auto* const tex{ texture["ptr"].get<Texture*>() };
				if (tex == nullptr)
					return;
				sprite->tex = tex;
				sprite->keepAlive = std::move(keepAlive);
				sprite->gpuData.pos = { x, y };
				if (args.size() == 0)
				{
					sprite->gpuData.useRegion = useAtlas ? 1u : 0u;
					if (useAtlas)
						sprite->gpuData.region = get_texture_region(texture);
					return;
				}
				if (args.size() != 4)
					throw sol::error("sprites.setup expects either no region or four region coordinates.");

				sprite->gpuData.useRegion = 1;
				const rectf region{
					args[0].get<float>(),
					args[1].get<float>(),
					args[2].get<float>(),
					args[3].get<float>()
				};
				sprite->gpuData.region = useAtlas ? get_texture_region(texture, region) : region;
			};

		sprite_table["update_position"] =
			[](const LuaSprite& handle, float x, float y, float z, float r)
			{
				auto sprite{ &handle.get() };
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
					const sol::table update{ updates.get<sol::table>(i) };
					const auto handle{ update.get<LuaSprite>("sprite") };
					auto sprite{ &handle.get() };
					sprite->gpuData.pos.x = update.get<float>("x");
					sprite->gpuData.pos.y = update.get<float>("y");
					sprite->gpuData.rotation = update.get<float>("r");
				}
			};

		sprite_table["update_positions_batch"] =
			[](const sol::table& updates, std::size_t count)
			{
				if (count > updates.size())
					throw sol::error("Sprite position batch count exceeds the update table size");

				for (std::size_t i = 1; i <= count; ++i)
				{
					const sol::table update{ updates.get<sol::table>(i) };
					const auto handle{ update.get<LuaSprite>("sprite") };
					auto sprite{ &handle.get() };
					sprite->gpuData.pos.x = update.get<float>("x");
					sprite->gpuData.pos.y = update.get<float>("y");
					sprite->gpuData.z = update.get<float>("z");
					sprite->gpuData.rotation = update.get<float>("r");
				}
			};

		sprite_table["render_atlas_batch"] =
			[&app](
				const sol::table& atlas_sprites,
				const sol::table& names,
				const sol::table& positions_x,
				const sol::table& positions_y,
				const sol::table& depths,
				std::size_t count,
				const std::string& prefix,
				float size
			)
			{
				if (count > names.size() || count > positions_x.size()
					|| count > positions_y.size() || count > depths.size())
					throw sol::error("Atlas sprite batch count exceeds an input table size");

				struct AtlasSprite
				{
					Texture* texture;
					std::shared_ptr<Texture> keep_alive;
					rectf region;
					float content;
				};
				std::unordered_map<std::string, AtlasSprite> icon_cache;
				icon_cache.reserve(32);
				std::vector<SpriteDefinition> sprites;
				sprites.reserve(count);
				for (std::size_t i = 1; i <= count; ++i)
				{
					const auto name{ names.get<std::string>(i) };
					auto icon{ icon_cache.find(name) };
					if (icon == icon_cache.end())
					{
						const auto descriptor{ atlas_sprites.get<sol::table>(prefix + name) };
						const auto texture{ descriptor.get<sol::table>("tex") };
						if (!texture["valid"].get_or(false))
							throw sol::error("Invalid atlas texture for sprite: " + name);

						auto* const tex{ texture["ptr"].get<Texture*>() };
						if (tex == nullptr)
							throw sol::error("Missing atlas texture for sprite: " + name);
						auto keep_alive{ get_texture_lease(texture) };
						if (texture["temp"].get_or(false) && !keep_alive)
							throw sol::error("Released atlas texture for sprite: " + name);

						AtlasSprite atlas_sprite{
							tex,
							std::move(keep_alive),
							{
								descriptor.get<float>("sx"),
								descriptor.get<float>("sy"),
								descriptor.get<float>("sw"),
								descriptor.get<float>("sh")
							},
							descriptor.get<float>("content")
						};
						icon = icon_cache.emplace(name, std::move(atlas_sprite)).first;
					}

					const auto& atlas_sprite{ icon->second };
					const float scale{ size / atlas_sprite.content };
					const float width{ atlas_sprite.region.z * scale };
					const float height{ atlas_sprite.region.w * scale };
					SpriteDefinition sprite{};
					sprite.tex = atlas_sprite.texture;
					sprite.keepAlive = atlas_sprite.keep_alive;
					sprite.gpuData.pos = {
						positions_x.get<float>(i) - width * 0.5f,
						positions_y.get<float>(i) - height * 0.5f
					};
					sprite.gpuData.region = atlas_sprite.region;
					sprite.gpuData.scale = { scale, scale };
					sprite.gpuData.z = depths.get<float>(i);
					sprite.gpuData.useRegion = 1;
					sprites.push_back(std::move(sprite));
				}

				app.window()->render_textures(std::move(sprites));
			};

		state.new_usertype<LuaSprite>
		(
			"sprite",
			sol::no_constructor,
			"pos", sol::property(
				[](LuaSprite& sprite) -> glm::vec2& { return sprite.get().gpuData.pos; },
				[](LuaSprite& sprite, const glm::vec2& pos) { sprite.get().gpuData.pos = pos; }
			),
			"region", sol::property(
				[](LuaSprite& sprite) -> rectf& { return sprite.get().gpuData.region; },
				[](LuaSprite& sprite, const rectf& region) { sprite.get().gpuData.region = region; }
			),
			"use_region", sol::property(
				[](const LuaSprite& sprite) { return sprite.get().gpuData.useRegion != 0; },
				[](LuaSprite& sprite, bool useRegion) { sprite.get().gpuData.useRegion = useRegion ? 1u : 0u; }
			),
			"z", sol::property(
				[](LuaSprite& sprite) { return sprite.get().gpuData.z; },
				[](LuaSprite& sprite, float z) { sprite.get().gpuData.z = z; }
			),
			"scale", sol::property(
				[](LuaSprite& sprite) -> glm::vec2& { return sprite.get().gpuData.scale; },
				[](LuaSprite& sprite, const glm::vec2& scale) { sprite.get().gpuData.scale = scale; }
			),
			"rotation", sol::property(
				[](LuaSprite& sprite) { return sprite.get().gpuData.rotation; },
				[](LuaSprite& sprite, float rotation) { sprite.get().gpuData.rotation = rotation; }
			),
			"tint", sol::property(
				[](LuaSprite& sprite) -> glm::vec4& { return sprite.get().gpuData.tint; },
				[](LuaSprite& sprite, const glm::vec4& tint) { sprite.get().gpuData.tint = tint; }
			)
		);

		app_table["sprites"] = sprite_table;
	}
}
