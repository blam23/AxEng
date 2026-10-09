#include "axeng/core/lua/bindings/lua_sprite_bindings.h"
#include "axeng/core/application.h"
#include "axeng/core/lua/bindings/lua_texture_bindings.h"
#include "spdlog/spdlog.h"

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
			sol::overload
			(
				[useAtlas = app.m_atlasTexture != nullptr](const LuaSprite& handle, const sol::table& texture, float x, float y, float rx, float ry, float rw, float rh)
				{
					auto sprite{ &handle.get() };
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
				[useAtlas = app.m_atlasTexture != nullptr](const LuaSprite& handle, const sol::table& texture, float x, float y)
				{
					auto sprite{ &handle.get() };
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
