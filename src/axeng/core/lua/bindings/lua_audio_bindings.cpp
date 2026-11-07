#include "axeng/core/lua/bindings/lua_audio_bindings.h"

#include "axeng/core/lua/bindings/lua_audio_definitions.h"

#include "axeng/core/application.h"
#include "axeng/core/audio/audio_system.h"
#include "axeng/core/audio/music.h"
#include "axeng/core/audio/sfx_synth.h"

#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace
{
	using ax::lua::bindings::fm_definition_from_table;
	using ax::lua::bindings::instrument_envelope_from_table;
	using ax::lua::bindings::sfx_params_from_table;
	using ax::lua::bindings::sfx_params_to_table;

	std::uint64_t to_seed(double value) noexcept
	{
		if (!std::isfinite(value))
			return 0;
		return static_cast<std::uint64_t>(static_cast<std::int64_t>(value));
	}

	sol::table string_list(sol::state_view lua, const std::vector<std::string>& values)
	{
		auto table{ lua.create_table(static_cast<int>(values.size()), 0) };
		for (std::size_t i{}; i < values.size(); ++i)
			table[i + 1] = values[i];
		return table;
	}

	std::optional<ax::audio::InstrumentWave> instrument_wave_from_name(std::string_view name) noexcept
	{
		if (name == "sine")
			return ax::audio::InstrumentWave::Sine;
		if (name == "triangle")
			return ax::audio::InstrumentWave::Triangle;
		if (name == "saw")
			return ax::audio::InstrumentWave::Saw;
		if (name == "square")
			return ax::audio::InstrumentWave::Square;
		if (name == "noise")
			return ax::audio::InstrumentWave::Noise;
		return std::nullopt;
	}

	std::optional<ax::audio::InstrumentPlayOptions> instrument_options_from_table(
		const sol::optional<sol::table>& table)
	{
		ax::audio::InstrumentPlayOptions options{};
		if (!table)
			return options;

		options.volume = table->get_or("volume", options.volume);
		options.pan = table->get_or("pan", options.pan);
		const sol::optional<std::string> bus{ (*table)["bus"] };
		if (bus)
		{
			const auto parsed{ ax::audio::bus_from_name(*bus) };
			if (!parsed || *parsed == ax::audio::Bus::Master)
			{
				spdlog::error("<Audio> Invalid instrument bus '{}'", *bus);
				return std::nullopt;
			}
			options.bus = *parsed;
		}
		return options;
	}

	std::optional<ax::audio::SynthInstrumentDefinition> synth_definition_from_table(const sol::table& table)
	{
		ax::audio::SynthInstrumentDefinition definition{};
		const sol::optional<std::string> wave{ table["wave"] };
		if (wave)
		{
			const auto parsed{ instrument_wave_from_name(*wave) };
			if (!parsed)
			{
				spdlog::error("<Audio> Unknown pitched synth wave '{}'", *wave);
				return std::nullopt;
			}
			definition.wave = *parsed;
		}
		definition.gain = table.get_or("gain", definition.gain);
		definition.detuneCents = table.get_or("detune_cents", definition.detuneCents);
		definition.vibratoCents = table.get_or("vibrato_cents", definition.vibratoCents);
		definition.vibratoRate = table.get_or("vibrato_rate", definition.vibratoRate);
		definition.lowPassHz = table.get_or("low_pass_hz", definition.lowPassHz);
		definition.pitchDropSemitones = table.get_or("pitch_drop_semitones", definition.pitchDropSemitones);
		definition.pitchDropSeconds = table.get_or("pitch_drop_seconds", definition.pitchDropSeconds);
		const sol::optional<sol::table> envelope{ table["envelope"] };
		if (envelope)
			definition.envelope = instrument_envelope_from_table(*envelope, definition.envelope);
		return definition;
	}

	ax::audio::SamplerInstrumentDefinition sampler_definition_from_table(const sol::table& table)
	{
		ax::audio::SamplerInstrumentDefinition definition{};
		definition.rootNote = table.get_or("root_note", definition.rootNote);
		definition.tuningCents = table.get_or("tuning_cents", definition.tuningCents);
		definition.startFrame = table.get_or("start_frame", definition.startFrame);
		definition.endFrame = table.get_or("end_frame", definition.endFrame);
		definition.loopStartFrame = table.get_or("loop_start_frame", definition.loopStartFrame);
		definition.loopEndFrame = table.get_or("loop_end_frame", definition.loopEndFrame);
		definition.gain = table.get_or("gain", definition.gain);
		definition.loop = table.get_or("loop", definition.loop);
		const sol::optional<sol::table> envelope{ table["envelope"] };
		if (envelope)
			definition.envelope = instrument_envelope_from_table(*envelope, definition.envelope);
		return definition;
	}

	bool read_array_index(const sol::object& key, int& index)
	{
		if (key.get_type() != sol::type::number)
			return false;
		const auto value{ key.as<double>() };
		if (!std::isfinite(value) || value < 1.0 || value > 16384.0 || std::floor(value) != value)
			return false;
		index = static_cast<int>(value);
		return true;
	}
}

void ax::lua::bindings::setup_audio_bindings(ax::Application& app, sol::state& state)
{
	auto* const system{ app.audio() };
	if (system == nullptr)
		return;

	auto app_table{ state["app"].get<sol::table>() };
	auto audio_table{ state.create_table() };
	app_table["audio"] = audio_table;

	// The audio system can be destroyed before the Lua state during shutdown, so always look it up.
	const auto with_audio{ [&app](auto&& func, auto fallback)
	{
		auto* const audio{ app.audio() };
		return audio != nullptr ? func(*audio) : fallback;
	} };

	audio_table["available"] = system->device_open();
	audio_table["sample_rate"] = system->sample_rate();

	audio_table["presets"] =
		[](sol::this_state lua) -> sol::table
		{
			std::vector<std::string> names{};
			for (const auto name : ax::audio::preset_names())
				names.emplace_back(name);
			return string_list(lua, names);
		};

	audio_table["params"] =
		[](sol::this_state lua) -> sol::table
		{
			sol::state_view view{ lua };
			const auto infos{ ax::audio::sfx_param_infos() };
			auto list{ view.create_table(static_cast<int>(infos.size()), 0) };
			for (std::size_t i{}; i < infos.size(); ++i)
				list[i + 1] = view.create_table_with("name", std::string{ infos[i].name }, "min", infos[i].min, "max", infos[i].max);
			return list;
		};

	audio_table["waves"] =
		[](sol::this_state lua) -> sol::table
		{
			std::vector<std::string> names{};
			for (std::size_t i{}; i < static_cast<std::size_t>(ax::audio::WaveType::Count); ++i)
				names.emplace_back(ax::audio::wave_type_name(static_cast<ax::audio::WaveType>(i)));
			return string_list(lua, names);
		};

	audio_table["waveform"] =
		[with_audio](sol::this_state lua, const std::string& name, sol::optional<int> buckets) -> sol::object
		{
			const auto wave{ with_audio([&](ax::audio::AudioSystem& audio)
			{
				return audio.sound_waveform(name, static_cast<std::uint32_t>(std::max(1, buckets.value_or(128))));
			}, std::vector<std::pair<float, float>>{}) };
			if (wave.empty())
				return sol::nil;
			sol::state_view view{ lua };
			auto lows{ view.create_table(static_cast<int>(wave.size()), 0) };
			auto highs{ view.create_table(static_cast<int>(wave.size()), 0) };
			for (std::size_t i{}; i < wave.size(); ++i)
			{
				lows[i + 1] = wave[i].first;
				highs[i + 1] = wave[i].second;
			}
			return view.create_table_with("min", lows, "max", highs);
		};

	audio_table["generate"] =
		[](sol::this_state lua, const std::string& preset, sol::optional<double> seed) -> sol::object
		{
			const auto params{ ax::audio::generate_preset(preset, seed ? to_seed(*seed) : 0) };
			if (!params)
				return sol::nil;
			return sfx_params_to_table(lua, *params);
		};

	audio_table["mutate"] =
		[](sol::this_state lua, const sol::table& params, sol::optional<float> amount, sol::optional<double> seed) -> sol::table
		{
			ax::audio::SfxParams base{};
			sfx_params_from_table(params, base);
			return sfx_params_to_table(lua, ax::audio::mutate(base, amount.value_or(0.05f), seed ? to_seed(*seed) : 0));
		};

	audio_table["fm_presets"] =
		[](sol::this_state lua) -> sol::table
		{
			std::vector<std::string> names{};
			for (const auto name : ax::audio::fm_preset_names())
				names.emplace_back(name);
			return string_list(lua, names);
		};

	// The fields of an FM sound effect from a preset: { fm = patch, note, velocity, hold, loop_length }.
	audio_table["generate_fm"] =
		[](sol::this_state state, const std::string& preset, sol::optional<double> seed) -> sol::object
		{
			const auto params{ ax::audio::generate_fm_preset(preset, seed ? to_seed(*seed) : 0) };
			if (!params)
				return sol::make_object(state, sol::nil);
			sol::state_view lua{ state };
			return lua.create_table_with("fm", ax::lua::bindings::fm_definition_to_table(lua, params->patch),
				"note", static_cast<int>(params->note), "velocity", params->velocity, "hold", params->hold,
				"loop_length", params->loopLength);
		};

	// Returns a nudged copy of an FM patch, as sound variants are made (see audio::mutate_fm).
	audio_table["mutate_fm"] =
		[](sol::this_state state, const sol::table& patch, sol::optional<float> amount, sol::optional<double> seed)
			-> sol::object
		{
			const auto definition{ fm_definition_from_table(patch) };
			if (!definition)
				return sol::make_object(state, sol::nil);
			const auto mutated{ ax::audio::mutate_fm(ax::audio::FmSfxParams{ .patch = *definition },
				amount.value_or(0.05f), seed ? to_seed(*seed) : 0) };
			return ax::lua::bindings::fm_definition_to_table(state, mutated.patch);
		};

	audio_table["define"] =
		[with_audio](const std::string& name, const sol::table& table, sol::optional<sol::table> options) -> bool
		{
			const auto def{ ax::lua::bindings::sound_definition_from_table(name, table) };
			if (!def)
				return false;
			const bool live{ options ? options->get_or("live", false) : false };
			return with_audio([&](ax::audio::AudioSystem& audio) { return audio.define_sound(name, *def, live); }, false);
		};

	audio_table["has"] =
		[with_audio](const std::string& name) -> bool
		{
			return with_audio([&](ax::audio::AudioSystem& audio) { return audio.has_sound(name); }, false);
		};

	audio_table["prepare"] =
		[with_audio](const std::string& name) -> bool
		{
			return with_audio([&](ax::audio::AudioSystem& audio) { return audio.prepare_sound(name); }, false);
		};

	audio_table["sounds"] =
		[with_audio](sol::this_state lua) -> sol::table
		{
			return string_list(lua, with_audio([](ax::audio::AudioSystem& audio) { return audio.sound_names(); }, std::vector<std::string>{}));
		};

	audio_table["info"] =
		[with_audio](sol::this_state lua, const std::string& name) -> sol::object
		{
			const auto info{ with_audio([&](ax::audio::AudioSystem& audio) { return audio.sound_info(name); }, std::optional<ax::audio::SoundInfo>{}) };
			if (!info)
				return sol::nil;
			sol::state_view view{ lua };
			return view.create_table_with(
				"variants", info->variants,
				"rendered_variants", info->renderedVariants,
				"length", info->length,
				"peak", info->peak,
				"rms", info->rms,
				"seam", info->seam,
				"fm", info->fm,
				"loop", info->loop,
				"bus", std::string{ ax::audio::bus_name(info->bus) });
		};

	audio_table["play"] =
		[with_audio](const std::string& name, sol::optional<sol::table> options) -> double
		{
			ax::audio::PlayOptions play{};
			if (options)
			{
				play.volume = options->get_or("volume", play.volume);
				play.pitch = options->get_or("pitch", play.pitch);
				play.pan = options->get_or("pan", play.pan);
				play.fadeIn = options->get_or("fade_in", play.fadeIn);
				const sol::optional<bool> loop{ (*options)["loop"] };
				if (loop)
					play.loop = *loop;
			}
			const auto id{ with_audio([&](ax::audio::AudioSystem& audio) { return audio.play(name, play); }, ax::audio::VoiceId{}) };
			return static_cast<double>(id);
		};

	audio_table["render_synth_sample"] =
		[default_sample_rate = std::min(system->sample_rate(), 48000u)](
			const sol::table& table, int note, sol::optional<sol::table> options)
			-> sol::optional<std::string>
		{
			if (note < 0 || note > 127)
			{
				spdlog::error("<Audio> Invalid MIDI note '{}' for synth sample rendering", note);
				return sol::nullopt;
			}
			const auto definition{ synth_definition_from_table(table) };
			if (!definition)
				return sol::nullopt;

			const auto holdSeconds{ options ? options->get_or("hold_seconds", 1.0f) : 1.0f };
			const auto velocity{ options ? options->get_or("velocity", 1.0f) : 1.0f };
			const auto sampleRate{ options ? options->get_or("sample_rate", default_sample_rate) : default_sample_rate };
			std::string error{};
			const auto pcm{ ax::audio::render_synth_sample(*definition, static_cast<std::uint8_t>(note),
				velocity, holdSeconds, sampleRate, error) };
			if (!pcm)
			{
				spdlog::error("<Audio> Failed to render synth sample: {}", error);
				return sol::nullopt;
			}
			const auto wav{ ax::AudioSample::encode_wav(*pcm, error) };
			if (!wav)
			{
				spdlog::error("<Audio> Failed to encode synth sample: {}", error);
				return sol::nullopt;
			}
			return std::string{ reinterpret_cast<const char*>(wav->data()), wav->size() };
		};

	audio_table["render_fm_sample"] =
		[default_sample_rate = std::min(system->sample_rate(), 48000u)](
			const sol::table& table, int note, sol::optional<sol::table> options)
			-> sol::optional<std::string>
		{
			if (note < 0 || note > 127)
			{
				spdlog::error("<Audio> Invalid MIDI note '{}' for FM sample rendering", note);
				return sol::nullopt;
			}
			const auto definition{ fm_definition_from_table(table) };
			if (!definition)
				return sol::nullopt;

			const auto holdSeconds{ options ? options->get_or("hold_seconds", 1.0f) : 1.0f };
			const auto velocity{ options ? options->get_or("velocity", 1.0f) : 1.0f };
			const auto sampleRate{ options ? options->get_or("sample_rate", default_sample_rate) : default_sample_rate };
			std::string error{};
			const auto pcm{ ax::audio::render_fm_sample(*definition, static_cast<std::uint8_t>(note),
				velocity, holdSeconds, sampleRate, error) };
			if (!pcm)
			{
				spdlog::error("<Audio> Failed to render FM sample: {}", error);
				return sol::nullopt;
			}
			const auto wav{ ax::AudioSample::encode_wav(*pcm, error) };
			if (!wav)
			{
				spdlog::error("<Audio> Failed to encode FM sample: {}", error);
				return sol::nullopt;
			}
			return std::string{ reinterpret_cast<const char*>(wav->data()), wav->size() };
		};

	audio_table["load_sample"] =
		[&app](const std::string& name, const std::string& encoded) -> bool
		{
			constexpr std::size_t max_encoded_sample_bytes{ 64u * 1024u * 1024u };
			if (name.empty() || encoded.empty() || encoded.size() > max_encoded_sample_bytes)
			{
				spdlog::error("<Audio> Sample name must be nonempty and encoded data must be 1..{} bytes",
					max_encoded_sample_bytes);
				return false;
			}
			if (app.audio_samples().get(name) != nullptr)
			{
				spdlog::error("<Audio> Sample '{}' is already loaded", name);
				return false;
			}

			std::vector<std::uint8_t> data{};
			data.reserve(encoded.size());
			for (const auto byte : encoded)
				data.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
			if (app.audio_samples().load_from_raw(name, {}, data) == nullptr)
			{
				spdlog::error("<Audio> Failed to load generated or imported sample '{}'", name);
				return false;
			}
			return true;
		};

	audio_table["sample_info"] =
		[&app](sol::this_state lua, const std::string& name) -> sol::object
		{
			const auto* const sample{ app.audio_samples().get(name) };
			if (sample == nullptr)
				return sol::make_object(lua, sol::nil);

			const auto& pcm{ sample->pcm() };
			sol::state_view view{ lua };
			return view.create_table_with(
				"sample_rate", pcm.sampleRate,
				"channels", pcm.channels,
				"frames", pcm.frame_count(),
				"duration_seconds", static_cast<double>(pcm.frame_count()) / pcm.sampleRate);
		};

	audio_table["inspect_sample"] =
		[](sol::this_state lua, const std::string& encoded) -> sol::object
		{
			constexpr std::size_t max_encoded_sample_bytes{ 64u * 1024u * 1024u };
			if (encoded.empty() || encoded.size() > max_encoded_sample_bytes)
			{
				spdlog::error("<Audio> Encoded sample must be between 1 byte and {} bytes",
					max_encoded_sample_bytes);
				return sol::make_object(lua, sol::nil);
			}
			const auto bytes{ std::span{
				reinterpret_cast<const std::uint8_t*>(encoded.data()), encoded.size() } };
			const auto pcm{ ax::audio::decode_audio_sample(bytes) };
			if (!pcm)
			{
				spdlog::error("<Audio> Encoded sample is empty, unsupported or invalid");
				return sol::make_object(lua, sol::nil);
			}
			sol::state_view view{ lua };
			return view.create_table_with(
				"sample_rate", pcm->sampleRate,
				"channels", pcm->channels,
				"frames", pcm->frame_count(),
				"duration_seconds", static_cast<double>(pcm->frame_count()) / pcm->sampleRate);
		};

	audio_table["play_synth"] =
		[with_audio](const sol::table& table, int note, sol::optional<sol::table> options) -> double
		{
			if (note < 0 || note > 127)
			{
				spdlog::error("<Audio> Invalid MIDI note '{}'", note);
				return 0.0;
			}
			const auto definition{ synth_definition_from_table(table) };
			const auto playback{ instrument_options_from_table(options) };
			if (!definition || !playback)
				return 0.0;
			const auto velocity{ options ? options->get_or("velocity", 1.0f) : 1.0f };
			const auto id{ with_audio([&](ax::audio::AudioSystem& audio)
			{
				return audio.play_synth(*definition, static_cast<std::uint8_t>(note), velocity, *playback);
			}, ax::audio::VoiceId{}) };
			if (id == 0)
				spdlog::warn("<Audio> Pitched synth voice was rejected");
			return static_cast<double>(id);
		};

	audio_table["fm_algorithm"] =
		[](sol::this_state state, int number) -> sol::object
		{
			if (number < 1 || number > ax::audio::fm_algorithm_count)
				return sol::make_object(state, sol::nil);
			const auto& algorithm{ *ax::audio::fm_algorithm(static_cast<std::uint8_t>(number)) };
			sol::state_view lua{ state };
			const auto operator_list{ [&lua](std::uint32_t mask)
			{
				auto list{ lua.create_table() };
				for (int op{ 1 }; op <= static_cast<int>(ax::audio::fm_operator_count); ++op)
					if ((mask >> (op - 1)) & 1u)
						list.add(op);
				return list;
			} };
			auto modulators{ lua.create_table() };
			for (const std::uint32_t mask : algorithm.modulators)
				modulators.add(operator_list(mask));
			auto result{ lua.create_table() };
			result["modulators"] = modulators;
			result["carriers"] = operator_list(algorithm.carriers);
			result["feedback_target"] = algorithm.feedbackTarget + 1;
			result["feedback_source"] = algorithm.feedbackSource + 1;
			return result;
		};

	audio_table["play_fm"] =
		[with_audio](const sol::table& table, int note, sol::optional<sol::table> options) -> double
		{
			if (note < 0 || note > 127)
			{
				spdlog::error("<Audio> Invalid MIDI note '{}'", note);
				return 0.0;
			}
			const auto definition{ fm_definition_from_table(table) };
			const auto playback{ instrument_options_from_table(options) };
			if (!definition || !playback)
				return 0.0;
			const auto velocity{ options ? options->get_or("velocity", 1.0f) : 1.0f };
			const auto id{ with_audio([&](ax::audio::AudioSystem& audio)
			{
				return audio.play_fm(*definition, static_cast<std::uint8_t>(note), velocity, *playback);
			}, ax::audio::VoiceId{}) };
			if (id == 0)
				spdlog::warn("<Audio> FM voice was rejected");
			return static_cast<double>(id);
		};

	audio_table["play_sampler"] =
		[&app, with_audio](const std::string& sample_name, const sol::table& table, int note,
			sol::optional<sol::table> options) -> double
		{
			if (note < 0 || note > 127)
			{
				spdlog::error("<Audio> Invalid MIDI note '{}'", note);
				return 0.0;
			}
			const auto* const sample{ app.audio_samples().get(sample_name) };
			if (sample == nullptr)
			{
				spdlog::error("<Audio> Unknown audio sample '{}'", sample_name);
				return 0.0;
			}
			const auto definition{ sampler_definition_from_table(table) };
			const auto playback{ instrument_options_from_table(options) };
			if (!playback)
				return 0.0;
			const auto velocity{ options ? options->get_or("velocity", 1.0f) : 1.0f };
			const auto id{ with_audio([&](ax::audio::AudioSystem& audio)
			{
				return audio.play_sampler(sample->pcm(), definition, static_cast<std::uint8_t>(note), velocity, *playback);
			}, ax::audio::VoiceId{}) };
			if (id == 0)
				spdlog::warn("<Audio> Pitched sampler voice was rejected for '{}'", sample_name);
			return static_cast<double>(id);
		};

	audio_table["note_off"] =
		[with_audio](double id) -> bool
		{
			return with_audio([&](ax::audio::AudioSystem& audio)
			{
				return audio.note_off(static_cast<ax::audio::VoiceId>(id));
			}, false);
		};

	audio_table["set"] =
		[with_audio](double id, const sol::table& options) -> bool
		{
			const auto read_optional{ [&options](const char* key) -> std::optional<float>
			{
				const sol::optional<float> value{ options[key] };
				return value ? std::optional<float>{ *value } : std::nullopt;
			} };
			ax::audio::VoiceUpdate update{};
			update.volume = read_optional("volume");
			update.pitch = read_optional("pitch");
			update.pan = read_optional("pan");
			update.ramp = options.get_or("ramp", update.ramp);
			return with_audio([&](ax::audio::AudioSystem& audio) { return audio.update_voice(static_cast<ax::audio::VoiceId>(id), update); }, false);
		};

	audio_table["stop"] =
		[with_audio](double id, sol::optional<float> fade)
		{
			with_audio([&](ax::audio::AudioSystem& audio) { audio.stop(static_cast<ax::audio::VoiceId>(id), fade.value_or(0.05f)); return 0; }, 0);
		};

	audio_table["stop_sound"] =
		[with_audio](const std::string& name, sol::optional<float> fade)
		{
			with_audio([&](ax::audio::AudioSystem& audio) { audio.stop_sound(name, fade.value_or(0.05f)); return 0; }, 0);
		};

	audio_table["stop_all"] =
		[with_audio](sol::optional<float> fade)
		{
			with_audio([&](ax::audio::AudioSystem& audio) { audio.stop_all(fade.value_or(0.05f)); return 0; }, 0);
		};

	audio_table["is_playing"] =
		[with_audio](double id) -> bool
		{
			return with_audio([&](ax::audio::AudioSystem& audio) { return audio.is_playing(static_cast<ax::audio::VoiceId>(id)); }, false);
		};

	audio_table["voice_count"] =
		[with_audio]() -> std::size_t
		{
			return with_audio([](ax::audio::AudioSystem& audio) { return audio.voice_count(); }, std::size_t{});
		};

	audio_table["set_volume"] =
		[with_audio](const std::string& bus, float volume) -> bool
		{
			const auto parsed{ ax::audio::bus_from_name(bus) };
			if (!parsed)
			{
				spdlog::warn("<Audio> Unknown bus '{}'", bus);
				return false;
			}
			return with_audio([&](ax::audio::AudioSystem& audio) { audio.set_bus_volume(*parsed, volume); return true; }, false);
		};

	audio_table["get_volume"] =
		[with_audio](const std::string& bus) -> sol::optional<float>
		{
			const auto parsed{ ax::audio::bus_from_name(bus) };
			if (!parsed)
				return sol::nullopt;
			return with_audio([&](ax::audio::AudioSystem& audio) { return sol::optional<float>{ audio.bus_volume(*parsed) }; }, sol::optional<float>{});
		};

	//
	// Music
	//
	auto music_table{ state.create_table() };
	audio_table["music"] = music_table;

	const auto read_playlist_options{ [](const sol::optional<sol::table>& options, ax::audio::PlaylistOptions base)
	{
		if (options)
		{
			base.fade = options->get_or("fade", base.fade);
			base.gap = options->get_or("gap", base.gap);
			base.shuffle = options->get_or("shuffle", base.shuffle);
			base.repeat = options->get_or("loop", base.repeat);
		}
		return base;
	} };

	music_table["list"] =
		[&app](sol::this_state lua) -> sol::table
		{
			std::vector<std::string> names{};
			app.music().for_each_name([&names](const std::string& name) { names.push_back(name); });
			return string_list(lua, names);
		};

	music_table["info"] =
		[&app](sol::this_state lua, const std::string& name) -> sol::object
		{
			const auto* music{ app.music().get(name) };
			if (music == nullptr)
				return sol::nil;
			sol::state_view view{ lua };
			return view.create_table_with(
				"length", music->length_seconds(),
				"sample_rate", music->sample_rate(),
				"channels", music->channels());
		};

	music_table["play"] =
		[&app, with_audio, read_playlist_options](const std::string& name, sol::optional<sol::table> options) -> bool
		{
			const auto* music{ app.music().get(name) };
			if (music == nullptr)
			{
				spdlog::warn("<Audio> Unknown music '{}'", name);
				return false;
			}
			auto playlist{ read_playlist_options(options, ax::audio::PlaylistOptions{ .shuffle = false, .repeat = true }) };
			playlist.shuffle = false;
			return with_audio([&](ax::audio::AudioSystem& audio) { return audio.play_playlist({ music }, playlist); }, false);
		};

	music_table["shuffle"] =
		[&app, with_audio, read_playlist_options](sol::optional<sol::table> names, sol::optional<sol::table> options) -> bool
		{
			std::vector<const ax::Music*> tracks{};
			if (names)
			{
				for (const auto& entry : *names)
				{
					const auto name{ entry.second.as<std::string>() };
					if (const auto* music{ app.music().get(name) })
						tracks.push_back(music);
					else
						spdlog::warn("<Audio> Unknown music '{}'", name);
				}
			}
			else
			{
				app.music().for_each([&tracks](const std::string&, ax::Music& music) { tracks.push_back(&music); });
			}
			const auto playlist{ read_playlist_options(options, ax::audio::PlaylistOptions{}) };
			return with_audio([&](ax::audio::AudioSystem& audio) { return audio.play_playlist(std::move(tracks), playlist); }, false);
		};

	music_table["next"] =
		[with_audio](sol::optional<float> fade) -> bool
		{
			return with_audio([&](ax::audio::AudioSystem& audio) { return audio.next_track(fade.value_or(1.0f)); }, false);
		};

	music_table["previous"] =
		[with_audio](sol::optional<float> fade) -> bool
		{
			return with_audio([&](ax::audio::AudioSystem& audio)
			{
				return audio.previous_track(fade.value_or(1.0f));
			}, false);
		};

	music_table["stop"] =
		[with_audio](sol::optional<float> fade)
		{
			with_audio([&](ax::audio::AudioSystem& audio) { audio.stop_music(fade.value_or(1.0f)); return 0; }, 0);
		};

	music_table["pause"] =
		[with_audio]()
		{
			with_audio([](ax::audio::AudioSystem& audio) { audio.set_music_paused(true); return 0; }, 0);
		};

	music_table["resume"] =
		[with_audio]()
		{
			with_audio([](ax::audio::AudioSystem& audio) { audio.set_music_paused(false); return 0; }, 0);
		};

	music_table["paused"] =
		[with_audio]() -> bool
		{
			return with_audio([](ax::audio::AudioSystem& audio) { return audio.music_paused(); }, false);
		};

	music_table["current"] =
		[with_audio]() -> sol::optional<std::string>
		{
			auto name{ with_audio([](ax::audio::AudioSystem& audio) { return audio.current_music(); }, std::string{}) };
			if (name.empty())
				return sol::nullopt;
			return name;
		};

	music_table["playing"] =
		[with_audio]() -> bool
		{
			return with_audio([](ax::audio::AudioSystem& audio)
			{
				return audio.playlist_active() || !audio.current_music().empty();
			}, false);
		};

	auto sequence_table{ state.create_table() };
	audio_table["sequence"] = sequence_table;

	sequence_table["list"] =
		[with_audio](sol::this_state lua) -> sol::table
		{
			return string_list(lua, with_audio(
				[](ax::audio::AudioSystem& audio) { return audio.arrangement_names(); }, std::vector<std::string>{}));
		};

	sequence_table["define"] =
		[&app, with_audio](const std::string& name, const sol::table& table) -> bool
		{
			ax::audio::Arrangement arrangement{};
			arrangement.tempo = table.get_or("tempo", arrangement.tempo);
			arrangement.durationBeats = table.get_or("duration_beats", arrangement.durationBeats);
			if (const sol::optional<sol::table> reverb{ table["reverb"] })
			{
				arrangement.reverb.wet = reverb->get_or("wet", arrangement.reverb.wet);
				arrangement.reverb.decaySeconds = reverb->get_or("decay_seconds", arrangement.reverb.decaySeconds);
				arrangement.reverb.damping = reverb->get_or("damping", arrangement.reverb.damping);
			}

			const sol::optional<sol::table> instrumentTable{ table["instruments"] };
			if (!instrumentTable)
			{
				spdlog::error("<Audio> Arrangement '{}' is missing its instruments table", name);
				return false;
			}
			std::map<int, sol::table> orderedInstruments{};
			for (const auto& entry : *instrumentTable)
			{
				int index{};
				if (!read_array_index(entry.first, index) || orderedInstruments.contains(index))
				{
					spdlog::error("<Audio> Arrangement '{}' has an invalid instrument list index", name);
					return false;
				}
				orderedInstruments.emplace(index, entry.second.as<sol::table>());
			}
			if (orderedInstruments.empty() || orderedInstruments.size() > ax::audio::arrangement_max_instruments)
			{
				spdlog::error("<Audio> Arrangement '{}' must have between 1 and {} instruments",
					name, ax::audio::arrangement_max_instruments);
				return false;
			}
			arrangement.instruments.reserve(orderedInstruments.size());
			for (int index{ 1 }; index <= static_cast<int>(orderedInstruments.size()); ++index)
			{
				const auto it{ orderedInstruments.find(index) };
				if (it == orderedInstruments.end())
				{
					spdlog::error("<Audio> Arrangement '{}' has a gap in its instrument list", name);
					return false;
				}
				const sol::optional<std::string> kind{ it->second["type"] };
				if (!kind)
				{
					spdlog::error("<Audio> Arrangement '{}' instrument {} is missing its type", name, index);
					return false;
				}
				if (*kind == "synth")
				{
					const auto definition{ synth_definition_from_table(it->second) };
					if (!definition)
						return false;
					arrangement.instruments.push_back({ .definition = *definition });
				}
				else if (*kind == "fm")
				{
					const auto definition{ fm_definition_from_table(it->second) };
					if (!definition)
						return false;
					arrangement.instruments.push_back({ .definition = *definition });
				}
				else if (*kind == "sample")
				{
					const sol::optional<std::string> sampleName{ it->second["sample"] };
					if (!sampleName)
					{
						spdlog::error("<Audio> Arrangement '{}' sampler {} is missing its sample name", name, index);
						return false;
					}
					const auto* const sample{ app.audio_samples().get(*sampleName) };
					if (sample == nullptr)
					{
						spdlog::error("<Audio> Arrangement '{}' references unknown audio sample '{}'", name, *sampleName);
						return false;
					}
					arrangement.instruments.push_back({
						.definition = sampler_definition_from_table(it->second),
						.sample = &sample->pcm() });
				}
				else
				{
					spdlog::error("<Audio> Arrangement '{}' instrument {} has unknown type '{}'", name, index, *kind);
					return false;
				}
				arrangement.instruments.back().chorusSend = it->second.get_or("chorus_send", 0.0f);
				arrangement.instruments.back().reverbSend = it->second.get_or("reverb_send", 1.0f);
			}

			const sol::optional<sol::table> noteTable{ table["notes"] };
			if (!noteTable)
			{
				spdlog::error("<Audio> Arrangement '{}' is missing its notes table", name);
				return false;
			}
			std::map<int, sol::table> orderedNotes{};
			for (const auto& entry : *noteTable)
			{
				int index{};
				if (!read_array_index(entry.first, index) || orderedNotes.contains(index))
				{
					spdlog::error("<Audio> Arrangement '{}' has an invalid note list index", name);
					return false;
				}
				orderedNotes.emplace(index, entry.second.as<sol::table>());
			}
			if (orderedNotes.size() > ax::audio::arrangement_max_notes)
			{
				spdlog::error("<Audio> Arrangement '{}' exceeds the {}-note limit",
					name, ax::audio::arrangement_max_notes);
				return false;
			}
			arrangement.notes.reserve(orderedNotes.size());
			for (int index{ 1 }; index <= static_cast<int>(orderedNotes.size()); ++index)
			{
				const auto it{ orderedNotes.find(index) };
				if (it == orderedNotes.end())
				{
					spdlog::error("<Audio> Arrangement '{}' has a gap in its note list", name);
					return false;
				}
				const auto& noteTableEntry{ it->second };
				const auto instrument{ noteTableEntry.get_or("instrument", 0) };
				const auto note{ noteTableEntry.get_or("note", -1) };
				if (instrument < 1 || instrument > static_cast<int>(arrangement.instruments.size())
					|| note < 0 || note > 127)
				{
					spdlog::error("<Audio> Arrangement '{}' note {} has an invalid instrument or MIDI pitch", name, index);
					return false;
				}
				arrangement.notes.push_back({
					.startBeat = noteTableEntry.get_or("start_beat", 0.0f),
					.durationBeats = noteTableEntry.get_or("duration_beats", 1.0f),
					.instrument = static_cast<std::uint32_t>(instrument - 1),
					.note = static_cast<std::uint8_t>(note),
					.velocity = noteTableEntry.get_or("velocity", 1.0f),
					.pan = noteTableEntry.get_or("pan", 0.0f) });
			}

			std::string error{};
			const auto defined{ with_audio([&](ax::audio::AudioSystem& audio)
			{
				return audio.define_arrangement(name, std::move(arrangement), error);
			}, false) };
			if (!defined && !error.empty())
				spdlog::error("<Audio> Failed to define arrangement '{}': {}", name, error);
			return defined;
		};

	sequence_table["play"] =
		[with_audio](const std::string& name, sol::optional<float> fade) -> bool
		{
			return with_audio([&](ax::audio::AudioSystem& audio)
			{
				return audio.play_arrangement(name, fade.value_or(0.1f));
			}, false);
		};

	sequence_table["stop"] =
		[with_audio](sol::optional<float> fade)
		{
			with_audio([&](ax::audio::AudioSystem& audio)
			{
				audio.stop_arrangement(fade.value_or(0.1f));
				return 0;
			}, 0);
		};

	sequence_table["pause"] =
		[with_audio]()
		{
			with_audio([](ax::audio::AudioSystem& audio)
			{
				audio.set_arrangement_paused(true);
				return 0;
			}, 0);
		};

	sequence_table["resume"] =
		[with_audio]()
		{
			with_audio([](ax::audio::AudioSystem& audio)
			{
				audio.set_arrangement_paused(false);
				return 0;
			}, 0);
		};

	sequence_table["seek"] =
		[with_audio](float seconds) -> bool
		{
			return with_audio([&](ax::audio::AudioSystem& audio)
			{
				return audio.seek_arrangement(seconds);
			}, false);
		};

	sequence_table["state"] =
		[with_audio](sol::this_state lua) -> sol::object
		{
			const auto info{ with_audio(
				[](ax::audio::AudioSystem& audio) { return audio.arrangement_playback(); },
				ax::audio::ArrangementPlaybackInfo{}) };
			if (info.name.empty())
				return sol::nil;
			sol::state_view view{ lua };
			return view.create_table_with(
				"name", info.name,
				"active", info.active,
				"paused", info.paused,
				"position_seconds", info.positionSeconds,
				"duration_seconds", info.durationSeconds);
		};
}
