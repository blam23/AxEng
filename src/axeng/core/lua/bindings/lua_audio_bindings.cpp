#include "axeng/core/lua/bindings/lua_audio_bindings.h"

#include "axeng/core/application.h"
#include "axeng/core/audio/audio_system.h"
#include "axeng/core/audio/music.h"
#include "axeng/core/audio/sfx_synth.h"

#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace
{
	std::uint64_t to_seed(double value) noexcept
	{
		if (!std::isfinite(value))
			return 0;
		return static_cast<std::uint64_t>(static_cast<std::int64_t>(value));
	}

	void apply_params(const sol::table& table, ax::audio::SfxParams& params)
	{
		const sol::object wave{ table["wave"] };
		if (wave.get_type() == sol::type::string)
		{
			const auto name{ wave.as<std::string>() };
			if (const auto type{ ax::audio::wave_type_from_name(name) })
				params.waveType = *type;
			else
				spdlog::warn("<Audio> Unknown wave type '{}'", name);
		}
		else if (wave.get_type() == sol::type::number)
		{
			const auto index{ wave.as<int>() };
			if (index >= 0 && index < static_cast<int>(ax::audio::WaveType::Count))
				params.waveType = static_cast<ax::audio::WaveType>(index);
		}

		for (const auto& info : ax::audio::sfx_param_infos())
		{
			const sol::optional<float> value{ table[info.name] };
			if (value)
				params.*info.member = *value;
		}
		ax::audio::clamp_params(params);
	}

	sol::table params_to_table(sol::state_view lua, const ax::audio::SfxParams& params)
	{
		auto table{ lua.create_table() };
		table["wave"] = std::string{ ax::audio::wave_type_name(params.waveType) };
		for (const auto& info : ax::audio::sfx_param_infos())
			table[info.name] = params.*info.member;
		return table;
	}

	std::optional<ax::audio::SoundDefinition> definition_from_table(const std::string& name, const sol::table& table)
	{
		ax::audio::SoundDefinition def{};
		const sol::optional<double> seed{ table["seed"] };
		def.seed = seed ? to_seed(*seed) : std::hash<std::string>{}(name);

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
		if (params)
			apply_params(*params, def.params);

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

	sol::table string_list(sol::state_view lua, const std::vector<std::string>& values)
	{
		auto table{ lua.create_table(static_cast<int>(values.size()), 0) };
		for (std::size_t i{}; i < values.size(); ++i)
			table[i + 1] = values[i];
		return table;
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
			return params_to_table(lua, *params);
		};

	audio_table["mutate"] =
		[](sol::this_state lua, const sol::table& params, sol::optional<float> amount, sol::optional<double> seed) -> sol::table
		{
			ax::audio::SfxParams base{};
			apply_params(params, base);
			return params_to_table(lua, ax::audio::mutate(base, amount.value_or(0.05f), seed ? to_seed(*seed) : 0));
		};

	audio_table["define"] =
		[with_audio](const std::string& name, const sol::table& table, sol::optional<sol::table> options) -> bool
		{
			const auto def{ definition_from_table(name, table) };
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
}
