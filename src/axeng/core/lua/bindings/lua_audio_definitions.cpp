#include "axeng/core/lua/bindings/lua_audio_definitions.h"

#include <array>
#include <cmath>
#include <functional>
#include <string_view>

namespace
{
	constexpr std::array<std::string_view, 4> s_curve_names{ "neg_lin", "neg_exp", "pos_exp", "pos_lin" };
	constexpr std::array<std::string_view, 6> s_lfo_wave_names{
		"triangle", "saw_down", "saw_up", "square", "sine", "sample_hold" };

	std::uint64_t to_seed(double value) noexcept
	{
		if (!std::isfinite(value))
			return 0;
		return static_cast<std::uint64_t>(static_cast<std::int64_t>(value));
	}

	template <typename Value>
	bool read_fm_integer(const sol::table& table, const char* key, int low, int high, Value& value)
	{
		const auto number{ table.get_or(key, static_cast<int>(value)) };
		if (number < low || number > high)
		{
			spdlog::error("<Audio> FM field '{}' must be {}..{}", key, low, high);
			return false;
		}
		value = static_cast<Value>(number);
		return true;
	}

	bool read_fm_stages(const sol::table& table, const char* key, std::array<std::uint8_t, 4>& stages)
	{
		const sol::optional<sol::table> values{ table[key] };
		if (!values)
			return true;
		for (std::size_t i{}; i < stages.size(); ++i)
		{
			const auto value{ values->get_or(static_cast<int>(i) + 1, -1) };
			if (value < 0 || value > 99)
			{
				spdlog::error("<Audio> FM '{}' needs four values from 0 to 99", key);
				return false;
			}
			stages[i] = static_cast<std::uint8_t>(value);
		}
		return true;
	}

	bool read_fm_envelope(const sol::table& table, ax::audio::FmEnvelope& envelope)
	{
		return read_fm_stages(table, "rates", envelope.rates) && read_fm_stages(table, "levels", envelope.levels);
	}

	template <typename Enum, std::size_t Count>
	bool read_fm_name(const sol::table& table, const char* key, const std::array<std::string_view, Count>& names,
		Enum& value, const char* what)
	{
		const sol::optional<std::string> name{ table[key] };
		if (!name)
			return true;
		for (std::size_t i{}; i < names.size(); ++i)
		{
			if (names[i] == *name)
			{
				value = static_cast<Enum>(i);
				return true;
			}
		}
		spdlog::error("<Audio> Unknown FM {} '{}'", what, *name);
		return false;
	}

	bool read_fm_operator(const sol::table& table, ax::audio::FmOperator& op)
	{
		op.fixedFrequency = table.get_or("fixed", op.fixedFrequency);
		return read_fm_envelope(table, op.envelope)
			&& read_fm_name(table, "left_curve", s_curve_names, op.levelScalingLeftCurve, "scaling curve")
			&& read_fm_name(table, "right_curve", s_curve_names, op.levelScalingRightCurve, "scaling curve")
			&& read_fm_integer(table, "output_level", 0, 99, op.outputLevel)
			&& read_fm_integer(table, "coarse", 0, 31, op.coarse)
			&& read_fm_integer(table, "fine", 0, 99, op.fine)
			&& read_fm_integer(table, "detune", -7, 7, op.detune)
			&& read_fm_integer(table, "velocity_sensitivity", 0, 7, op.velocitySensitivity)
			&& read_fm_integer(table, "amp_mod_sensitivity", 0, 3, op.ampModSensitivity)
			&& read_fm_integer(table, "rate_scaling", 0, 7, op.rateScaling)
			&& read_fm_integer(table, "breakpoint", 0, 99, op.levelScalingBreakpoint)
			&& read_fm_integer(table, "left_depth", 0, 99, op.levelScalingLeftDepth)
			&& read_fm_integer(table, "right_depth", 0, 99, op.levelScalingRightDepth);
	}

	sol::table stages_table(sol::state_view lua, const std::array<std::uint8_t, 4>& stages)
	{
		return lua.create_table_with(1, stages[0], 2, stages[1], 3, stages[2], 4, stages[3]);
	}

	sol::table fm_envelope_table(sol::state_view lua, const ax::audio::FmEnvelope& envelope)
	{
		return lua.create_table_with("rates", stages_table(lua, envelope.rates),
			"levels", stages_table(lua, envelope.levels));
	}

	template <typename Value>
	void set_unless(sol::table& table, const char* key, Value value, Value fallback)
	{
		if (value != fallback)
			table[key] = static_cast<int>(value);
	}
}

bool ax::lua::bindings::sfx_params_from_table(const sol::table& table, ax::audio::SfxParams& params)
{
	bool ok{ true };
	const sol::object wave{ table["wave"] };
	if (wave.get_type() == sol::type::string)
	{
		const auto name{ wave.as<std::string>() };
		if (const auto type{ ax::audio::wave_type_from_name(name) })
			params.waveType = *type;
		else
		{
			spdlog::error("<Audio> Unknown wave type '{}'", name);
			ok = false;
		}
	}
	else if (wave.get_type() == sol::type::number)
	{
		const auto index{ wave.as<int>() };
		if (index >= 0 && index < static_cast<int>(ax::audio::WaveType::Count))
			params.waveType = static_cast<ax::audio::WaveType>(index);
	}

	for (const auto& info : ax::audio::sfx_param_infos())
	{
		const sol::object value{ table[info.name] };
		if (value.get_type() == sol::type::number)
			params.*info.member = value.as<float>();
	}
	ax::audio::clamp_params(params);
	return ok;
}

sol::table ax::lua::bindings::sfx_params_to_table(sol::state_view lua, const ax::audio::SfxParams& params)
{
	auto table{ lua.create_table() };
	table["wave"] = std::string{ ax::audio::wave_type_name(params.waveType) };
	for (const auto& info : ax::audio::sfx_param_infos())
		table[info.name] = params.*info.member;
	return table;
}

ax::audio::InstrumentEnvelope ax::lua::bindings::instrument_envelope_from_table(const sol::table& table,
	ax::audio::InstrumentEnvelope envelope)
{
	envelope.attack = table.get_or("attack", envelope.attack);
	envelope.decay = table.get_or("decay", envelope.decay);
	envelope.sustain = table.get_or("sustain", envelope.sustain);
	envelope.release = table.get_or("release", envelope.release);
	return envelope;
}

std::optional<ax::audio::FmInstrumentDefinition> ax::lua::bindings::fm_definition_from_table(const sol::table& table)
{
	ax::audio::FmInstrumentDefinition definition{};
	definition.gain = table.get_or("gain", definition.gain);
	definition.outputTrim = table.get_or("output_trim", definition.outputTrim);
	const sol::optional<sol::table> envelope{ table["envelope"] };
	if (envelope)
		definition.envelope = instrument_envelope_from_table(*envelope, definition.envelope);
	if (!read_fm_integer(table, "algorithm", 1, ax::audio::fm_algorithm_count, definition.algorithm)
		|| !read_fm_integer(table, "feedback", 0, 7, definition.feedback)
		|| !read_fm_integer(table, "transpose", -24, 24, definition.transpose))
		return std::nullopt;

	const sol::optional<sol::table> pitchEnvelope{ table["pitch_envelope"] };
	if (pitchEnvelope && !read_fm_envelope(*pitchEnvelope, definition.pitchEnvelope))
		return std::nullopt;

	const sol::optional<sol::table> lfo{ table["lfo"] };
	if (lfo && (!read_fm_name(*lfo, "wave", s_lfo_wave_names, definition.lfo.wave, "LFO wave")
		|| !read_fm_integer(*lfo, "speed", 0, 99, definition.lfo.speed)
		|| !read_fm_integer(*lfo, "delay", 0, 99, definition.lfo.delay)
		|| !read_fm_integer(*lfo, "pitch_depth", 0, 99, definition.lfo.pitchModDepth)
		|| !read_fm_integer(*lfo, "amp_depth", 0, 99, definition.lfo.ampModDepth)
		|| !read_fm_integer(*lfo, "pitch_sensitivity", 0, 7, definition.lfo.pitchModSensitivity)))
		return std::nullopt;

	const sol::optional<sol::table> operators{ table["operators"] };
	if (!operators)
	{
		spdlog::error("<Audio> FM definition is missing its operators");
		return std::nullopt;
	}
	for (std::size_t i{}; i < definition.operators.size(); ++i)
	{
		const sol::optional<sol::table> op{ (*operators)[static_cast<int>(i) + 1] };
		if (!op)
		{
			spdlog::error("<Audio> FM definitions need all {} operators", definition.operators.size());
			return std::nullopt;
		}
		if (!read_fm_operator(*op, definition.operators[i]))
			return std::nullopt;
	}
	return definition;
}

sol::table ax::lua::bindings::fm_definition_to_table(sol::state_view lua,
	const ax::audio::FmInstrumentDefinition& definition)
{
	const ax::audio::FmInstrumentDefinition defaults{};
	const ax::audio::FmOperator defaultOp{};
	auto table{ lua.create_table() };
	table["type"] = "fm";
	table["algorithm"] = static_cast<int>(definition.algorithm);
	set_unless(table, "feedback", definition.feedback, defaults.feedback);
	set_unless(table, "transpose", definition.transpose, defaults.transpose);
	table["gain"] = definition.gain;
	if (definition.outputTrim != defaults.outputTrim)
		table["output_trim"] = definition.outputTrim;
	const auto& gate{ definition.envelope };
	table["envelope"] = lua.create_table_with("attack", gate.attack, "decay", gate.decay,
		"sustain", gate.sustain, "release", gate.release);

	const auto& pitch{ definition.pitchEnvelope };
	if (pitch.rates != defaults.pitchEnvelope.rates || pitch.levels != defaults.pitchEnvelope.levels)
		table["pitch_envelope"] = fm_envelope_table(lua, pitch);

	const auto& lfo{ definition.lfo };
	auto lfoTable{ lua.create_table() };
	if (lfo.wave != defaults.lfo.wave)
		lfoTable["wave"] = std::string{ s_lfo_wave_names[static_cast<std::size_t>(lfo.wave)] };
	set_unless(lfoTable, "speed", lfo.speed, defaults.lfo.speed);
	set_unless(lfoTable, "delay", lfo.delay, defaults.lfo.delay);
	set_unless(lfoTable, "pitch_depth", lfo.pitchModDepth, defaults.lfo.pitchModDepth);
	set_unless(lfoTable, "amp_depth", lfo.ampModDepth, defaults.lfo.ampModDepth);
	set_unless(lfoTable, "pitch_sensitivity", lfo.pitchModSensitivity, defaults.lfo.pitchModSensitivity);
	if (!lfoTable.empty())
		table["lfo"] = lfoTable;

	auto operators{ lua.create_table() };
	for (std::size_t i{}; i < definition.operators.size(); ++i)
	{
		const auto& op{ definition.operators[i] };
		auto entry{ fm_envelope_table(lua, op.envelope) };
		entry["output_level"] = static_cast<int>(op.outputLevel);
		entry["coarse"] = static_cast<int>(op.coarse);
		entry["fine"] = static_cast<int>(op.fine);
		if (op.fixedFrequency)
			entry["fixed"] = true;
		set_unless(entry, "detune", op.detune, defaultOp.detune);
		set_unless(entry, "velocity_sensitivity", op.velocitySensitivity, defaultOp.velocitySensitivity);
		set_unless(entry, "amp_mod_sensitivity", op.ampModSensitivity, defaultOp.ampModSensitivity);
		set_unless(entry, "rate_scaling", op.rateScaling, defaultOp.rateScaling);
		set_unless(entry, "breakpoint", op.levelScalingBreakpoint, defaultOp.levelScalingBreakpoint);
		set_unless(entry, "left_depth", op.levelScalingLeftDepth, defaultOp.levelScalingLeftDepth);
		set_unless(entry, "right_depth", op.levelScalingRightDepth, defaultOp.levelScalingRightDepth);
		if (op.levelScalingLeftCurve != defaultOp.levelScalingLeftCurve)
			entry["left_curve"] = std::string{ s_curve_names[static_cast<std::size_t>(op.levelScalingLeftCurve)] };
		if (op.levelScalingRightCurve != defaultOp.levelScalingRightCurve)
			entry["right_curve"] = std::string{ s_curve_names[static_cast<std::size_t>(op.levelScalingRightCurve)] };
		operators[i + 1] = entry;
	}
	table["operators"] = operators;
	return table;
}

std::optional<ax::audio::SoundDefinition> ax::lua::bindings::sound_definition_from_table(const std::string& name,
	const sol::table& table)
{
	ax::audio::SoundDefinition def{};
	const sol::optional<double> seed{ table["seed"] };
	def.seed = seed ? to_seed(*seed) : std::hash<std::string>{}(name);

	const sol::optional<std::string> fmPreset{ table["fm_preset"] };
	const sol::optional<sol::table> fm{ table["fm"] };
	if (fmPreset || fm)
	{
		ax::audio::FmSfxParams params{};
		if (fmPreset)
		{
			const auto generated{ ax::audio::generate_fm_preset(*fmPreset, def.seed) };
			if (!generated)
			{
				spdlog::error("<Audio> Sound '{}' uses unknown FM preset '{}'", name, *fmPreset);
				return std::nullopt;
			}
			params = *generated;
		}
		if (fm)
		{
			const auto patch{ fm_definition_from_table(*fm) };
			if (!patch)
			{
				spdlog::error("<Audio> Sound '{}' has an invalid FM patch", name);
				return std::nullopt;
			}
			params.patch = *patch;
		}
		const auto note{ table.get_or("note", static_cast<int>(params.note)) };
		if (note < 0 || note > 127)
		{
			spdlog::error("<Audio> Sound '{}' needs a MIDI note from 0 to 127", name);
			return std::nullopt;
		}
		params.note = static_cast<std::uint8_t>(note);
		params.velocity = table.get_or("velocity", params.velocity);
		params.hold = table.get_or("hold", params.hold);
		params.loopLength = table.get_or("loop_length", params.loopLength);
		ax::audio::clamp_fm_sfx(params);
		def.fm = params;
	}
	else
	{
		const sol::optional<std::string> preset{ table["preset"] };
		if (preset)
		{
			const auto generated{ ax::audio::generate_preset(*preset, def.seed) };
			if (!generated)
			{
				spdlog::error("<Audio> Sound '{}' uses unknown preset '{}'", name, *preset);
				return std::nullopt;
			}
			def.params = *generated;
		}

		const sol::optional<sol::table> params{ table["params"] };
		if (params && !sfx_params_from_table(*params, def.params))
			return std::nullopt;
	}

	def.mutation = table.get_or("mutation", def.mutation);
	def.variants = table.get_or("variants", def.mutation > 0.0f ? 6u : 1u);
	def.pitchVariation = table.get_or("pitch_variation", def.pitchVariation);
	def.volume = table.get_or("volume", def.volume);
	def.volumeVariation = table.get_or("volume_variation", def.volumeVariation);
	def.maxInstances = table.get_or("max_instances", def.maxInstances);
	def.cooldown = table.get_or("cooldown", def.cooldown);
	def.loop = table.get_or("loop", def.loop);

	const sol::optional<std::string> bus{ table["bus"] };
	if (bus)
	{
		const auto parsed{ ax::audio::bus_from_name(*bus) };
		if (!parsed || *parsed == ax::audio::Bus::Master || *parsed == ax::audio::Bus::Music)
		{
			spdlog::error("<Audio> Sound '{}' uses invalid bus '{}' (use 'sfx' or 'ui')", name, *bus);
			return std::nullopt;
		}
		def.bus = *parsed;
	}
	return def;
}
