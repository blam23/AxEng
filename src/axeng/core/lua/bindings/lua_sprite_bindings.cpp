#include "axeng/core/lua/bindings/lua_sprite_bindings.h"
#include "axeng/core/application.h"
#include "axeng/core/lua/bindings/lua_texture_bindings.h"
#include "spdlog/spdlog.h"

void ax::lua::bindings::setup_sprite_bindings(ax::Application& app, sol::state& state)
{
	auto app_table{ state["app"].get<sol::table>() };
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
			[useAtlas = app.m_atlasTexture != nullptr]
			(
				SpriteDefinition* sprite,
				const sol::table& texture,
				float x,
				float y,
				sol::variadic_args args
			)
			{
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

		app_table["sprites"] = sprite_table;
	}
}
