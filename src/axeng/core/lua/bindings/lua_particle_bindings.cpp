#include "axeng/core/lua/bindings/lua_particle_bindings.h"

#include "axeng/core/application.h"
#include "axeng/core/particles/particle_system.h"
#include "axeng/core/window.h"

#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <string_view>

namespace
{
	float number_field(const sol::table& table, std::string_view key, float fallback)
	{
		const sol::object value{ table[key] };
		if (value == sol::nil)
			return fallback;
		if (value.get_type() != sol::type::number)
			throw sol::error(std::format("particle parameter '{}' must be a number", key));
		return value.as<float>();
	}

	float array_number_field(const sol::table& table, std::size_t index, float fallback)
	{
		const sol::object value{ table[index] };
		if (value == sol::nil)
			return fallback;
		if (value.get_type() != sol::type::number)
			throw sol::error(std::format("particle color component {} must be a number", index));
		return value.as<float>();
	}

	bool boolean_field(const sol::table& table, std::string_view key, bool fallback)
	{
		const sol::object value{ table[key] };
		if (value == sol::nil)
			return fallback;
		if (value.get_type() != sol::type::boolean)
			throw sol::error(std::format("particle parameter '{}' must be a boolean", key));
		return value.as<bool>();
	}

	std::uint32_t uint_field(const sol::table& table, std::string_view key, std::uint32_t fallback)
	{
		const sol::object value{ table[key] };
		if (value == sol::nil)
			return fallback;
		if (value.get_type() != sol::type::number)
			throw sol::error(std::format("particle parameter '{}' must be an integer", key));
		const auto number{ value.as<double>() };
		if (!std::isfinite(number) || number < 0.0 || number > std::numeric_limits<std::uint32_t>::max()
			|| std::floor(number) != number)
			throw sol::error(std::format("particle parameter '{}' must be a non-negative 32-bit integer", key));
		return static_cast<std::uint32_t>(number);
	}

	void vector2_field(const sol::table& table, std::string_view key, glm::vec2& value)
	{
		const sol::object object{ table[key] };
		if (object == sol::nil)
			return;
		if (object.get_type() != sol::type::table)
			throw sol::error(std::format("particle parameter '{}' must be a table with x and y numbers", key));
		const auto vector{ object.as<sol::table>() };
		value.x = number_field(vector, "x", value.x);
		value.y = number_field(vector, "y", value.y);
	}

	void range_field(const sol::table& table, std::string_view key, float& minimum, float& maximum)
	{
		const sol::object object{ table[key] };
		if (object == sol::nil)
			return;
		if (object.get_type() != sol::type::table)
			throw sol::error(std::format("particle parameter '{}' must be a table with min and max numbers", key));
		const auto range{ object.as<sol::table>() };
		minimum = number_field(range, "min", minimum);
		maximum = number_field(range, "max", maximum);
	}

	void color_field(const sol::table& table, std::string_view key, glm::vec4& color)
	{
		const sol::object object{ table[key] };
		if (object == sol::nil)
			return;
		if (object.get_type() != sol::type::table)
			throw sol::error(std::format("particle parameter '{}' must be a color table", key));
		const auto value{ object.as<sol::table>() };
		color.r = number_field(value, "r", array_number_field(value, 1, color.r));
		color.g = number_field(value, "g", array_number_field(value, 2, color.g));
		color.b = number_field(value, "b", array_number_field(value, 3, color.b));
		color.a = number_field(value, "a", array_number_field(value, 4, color.a));
	}

	ax::ParticleConfig parse_config(const sol::table& table, ax::ParticleConfig config)
	{
		config.capacity = uint_field(table, "capacity", config.capacity);
		config.seed = uint_field(table, "seed", config.seed);
		config.emissionRate = number_field(table, "emission_rate", config.emissionRate);
		config.duration = number_field(table, "duration", config.duration);
		config.explosiveness = number_field(table, "explosiveness", config.explosiveness);
		config.loop = boolean_field(table, "loop", config.loop);
		config.playing = boolean_field(table, "playing", config.playing);
		config.visible = boolean_field(table, "visible", config.visible);
		config.localSpace = boolean_field(table, "local_space", config.localSpace);
		config.rotation = number_field(table, "rotation", config.rotation);
		config.direction = number_field(table, "direction", config.direction);
		config.spread = number_field(table, "spread", config.spread);
		config.spawnRadius = number_field(table, "spawn_radius", config.spawnRadius);
		config.lifetimeMin = number_field(table, "lifetime_min", config.lifetimeMin);
		config.lifetimeMax = number_field(table, "lifetime_max", config.lifetimeMax);
		config.speedMin = number_field(table, "speed_min", config.speedMin);
		config.speedMax = number_field(table, "speed_max", config.speedMax);
		config.sizeStart = number_field(table, "size_start", config.sizeStart);
		config.sizeEnd = number_field(table, "size_end", config.sizeEnd);
		config.z = number_field(table, "z", config.z);
		config.particleRotationMin = number_field(table, "particle_rotation_min", config.particleRotationMin);
		config.particleRotationMax = number_field(table, "particle_rotation_max", config.particleRotationMax);
		config.angularVelocityMin = number_field(table, "angular_velocity_min", config.angularVelocityMin);
		config.angularVelocityMax = number_field(table, "angular_velocity_max", config.angularVelocityMax);
		vector2_field(table, "position", config.position);
		vector2_field(table, "gravity", config.gravity);
		vector2_field(table, "spawn_size", config.spawnSize);
		range_field(table, "lifetime", config.lifetimeMin, config.lifetimeMax);
		range_field(table, "speed", config.speedMin, config.speedMax);
		range_field(table, "particle_rotation", config.particleRotationMin, config.particleRotationMax);
		range_field(table, "angular_velocity", config.angularVelocityMin, config.angularVelocityMax);
		const sol::object shape{ table["spawn_shape"] };
		if (shape != sol::nil)
		{
			if (shape.get_type() != sol::type::string)
				throw sol::error("particle parameter 'spawn_shape' must be 'point', 'circle', or 'rectangle'");
			const auto shapeName{ shape.as<std::string>() };
			if (shapeName == "point")
				config.spawnShape = ax::ParticleSpawnShape::Point;
			else if (shapeName == "circle")
				config.spawnShape = ax::ParticleSpawnShape::Circle;
			else if (shapeName == "rectangle")
				config.spawnShape = ax::ParticleSpawnShape::Rectangle;
			else
				throw sol::error("particle parameter 'spawn_shape' must be 'point', 'circle', or 'rectangle'");
		}
		color_field(table, "start_color", config.colorStart);
		color_field(table, "end_color", config.colorEnd);
		if (const auto error{ ax::validate_particle_config(config) })
			throw sol::error(*error);
		return config;
	}
}

void ax::lua::bindings::setup_particle_bindings(ax::Application& app, sol::state& state)
{
	state.new_usertype<ax::ParticleEmitter>
	(
		"particle_emitter",
		sol::no_constructor,
		"set_config",
		[](ax::ParticleEmitter& emitter, const sol::table& table)
		{
			emitter.set_config(parse_config(table, emitter.config()));
		},
		"set_position",
		[](ax::ParticleEmitter& emitter, float x, float y)
		{
			emitter.set_position({ x, y });
		},
		"set_rotation", &ax::ParticleEmitter::set_rotation,
		"play", &ax::ParticleEmitter::play,
		"stop", &ax::ParticleEmitter::stop,
		"clear", &ax::ParticleEmitter::clear,
		"set_visible", &ax::ParticleEmitter::set_visible,
		"finished", &ax::ParticleEmitter::finished
	);

	auto appTable{ state["app"].get<sol::table>() };
	auto particles{ state.create_table() };
	particles["create"] = [&app](const sol::table& config)
	{
		if (!app.has_window())
			throw sol::error("Particle emitters require a window.");
		return app.window()->particle_system().create(parse_config(config, ax::ParticleConfig{}));
	};
	appTable["particles"] = particles;
}
