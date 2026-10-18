#include <gtest/gtest.h>

#include "axeng/core/application.h"
#include "axeng/core/audio/audio_system.h"
#include "axeng/core/audio/sfx_synth.h"
#include "tests/main/log_capture.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using namespace ax::audio;

	float peak(const std::vector<float>& samples)
	{
		float result{};
		for (const auto s : samples)
			result = std::max(result, std::abs(s));
		return result;
	}

	AudioSystem make_offline_audio()
	{
		return AudioSystem{ AudioSystem::Config{ .openDevice = false, .manualMix = true, .sampleRate = 48000, .seed = 1 } };
	}

	// Mixes `seconds` of audio and returns the interleaved stereo output.
	std::vector<float> mix_for(AudioSystem& audio, float seconds)
	{
		std::vector<float> out(static_cast<std::size_t>(seconds * static_cast<float>(audio.sample_rate())) * 2);
		constexpr std::uint32_t block{ 512 };
		const auto frames{ static_cast<std::uint32_t>(out.size() / 2) };
		for (std::uint32_t done{}; done < frames; done += block)
			audio.mix(out.data() + static_cast<std::size_t>(done) * 2, std::min(block, frames - done));
		return out;
	}

	float channel_energy(const std::vector<float>& stereo, std::size_t channel)
	{
		float energy{};
		for (std::size_t i{ channel }; i < stereo.size(); i += 2)
			energy += stereo[i] * stereo[i];
		return energy;
	}

	SoundDefinition simple_sound(bool loop = false)
	{
		SoundDefinition def{};
		def.params.waveType = WaveType::Sine;
		def.params.baseFreq = 0.4f;
		def.params.sustain = 0.2f;
		def.params.decay = 0.1f;
		def.loop = loop;
		return def;
	}

	std::vector<std::uint8_t> make_wav(float seconds, float frequency, std::uint32_t rate = 22050)
	{
		const auto frames{ static_cast<std::uint32_t>(seconds * static_cast<float>(rate)) };
		const auto dataSize{ frames * 2 };
		std::vector<std::uint8_t> wav{};
		const auto put32{ [&wav](std::uint32_t v) { for (int i{}; i < 4; ++i) wav.push_back(static_cast<std::uint8_t>(v >> (i * 8))); } };
		const auto put16{ [&wav](std::uint16_t v) { wav.push_back(static_cast<std::uint8_t>(v)); wav.push_back(static_cast<std::uint8_t>(v >> 8)); } };
		const auto tag{ [&wav](const char* t) { wav.insert(wav.end(), t, t + 4); } };
		tag("RIFF"); put32(36 + dataSize); tag("WAVE");
		tag("fmt "); put32(16); put16(1); put16(1); put32(rate); put32(rate * 2); put16(2); put16(16);
		tag("data"); put32(dataSize);
		for (std::uint32_t i{}; i < frames; ++i)
		{
			const auto s{ std::sin(2.0f * std::numbers::pi_v<float> * frequency * static_cast<float>(i) / static_cast<float>(rate)) };
			put16(static_cast<std::uint16_t>(static_cast<std::int16_t>(s * 12000.0f)));
		}
		return wav;
	}

	class ScopedEnv
	{
	public:
		ScopedEnv(const char* name, const char* value)
			: m_name{ name }
		{
			char* old{ nullptr };
			std::size_t length{};
			if (_dupenv_s(&old, &length, name) == 0 && old != nullptr)
			{
				m_old = old;
				m_hadOld = true;
				std::free(old);
			}
			_putenv_s(name, value);
		}

		~ScopedEnv()
		{
			_putenv_s(m_name.c_str(), m_hadOld ? m_old.c_str() : "");
		}

	private:
		std::string m_name;
		std::string m_old{};
		bool m_hadOld{ false };
	};
}

TEST(SfxSynth, EveryPresetProducesBoundedAudibleSound)
{
	for (const auto name : preset_names())
	{
		for (std::uint64_t seed{}; seed < 8; ++seed)
		{
			const auto params{ generate_preset(name, seed) };
			ASSERT_TRUE(params.has_value()) << name;
			const auto samples{ synthesize(*params, { .seed = seed }) };
			ASSERT_FALSE(samples.empty()) << name << " seed " << seed;
			EXPECT_LE(samples.size(), static_cast<std::size_t>(sfx_max_length * sfx_sample_rate));
			EXPECT_TRUE(std::ranges::all_of(samples, [](float s) { return std::isfinite(s) && s >= -1.0f && s <= 1.0f; })) << name;
			// "random" may legitimately generate something extremely quiet.
			if (name != "random")
				EXPECT_GT(peak(samples), 0.01f) << name << " seed " << seed;
		}
	}
	EXPECT_FALSE(generate_preset("not_a_preset", 0).has_value());
}

TEST(SfxSynth, GenerationIsDeterministic)
{
	const auto a{ generate_preset("pickup", 42) };
	const auto b{ generate_preset("pickup", 42) };
	const auto c{ generate_preset("pickup", 43) };
	ASSERT_TRUE(a && b && c);
	EXPECT_EQ(*a, *b);
	EXPECT_NE(*a, *c);
	EXPECT_EQ(synthesize(*a, { .seed = 5 }), synthesize(*b, { .seed = 5 }));

	// Noise depends on the synth seed.
	const auto noise{ generate_preset("hiss", 1) };
	ASSERT_TRUE(noise);
	EXPECT_NE(synthesize(*noise, { .seed = 1 }), synthesize(*noise, { .seed = 2 }));
}

TEST(SfxSynth, EnvelopeControlsLength)
{
	SfxParams params{};
	params.attack = 0.0f;
	params.sustain = 0.3f;
	params.decay = 0.4f;
	// Envelope stage lengths are value^2 * 100000 samples.
	const auto expected{ static_cast<std::size_t>(0.3f * 0.3f * 100000.0f) + static_cast<std::size_t>(0.4f * 0.4f * 100000.0f) };
	const auto samples{ synthesize(params) };
	EXPECT_NEAR(static_cast<double>(samples.size()), static_cast<double>(expected), 4.0);

	params.sustain = 0.0f;
	params.decay = 0.0f;
	EXPECT_TRUE(synthesize(params).empty());
}

TEST(SfxSynth, LoopsAreSeamless)
{
	for (const auto name : { "hum", "grind", "hiss" })
	{
		const auto params{ generate_preset(name, 7) };
		ASSERT_TRUE(params);
		const auto samples{ synthesize(*params, { .loop = true, .seed = 3 }) };
		ASSERT_GE(samples.size(), static_cast<std::size_t>(sfx_sample_rate / 4)) << name;
		// The wrap from the last sample to the first must look like any other step in the signal.
		float maxStep{};
		for (std::size_t i{ 1 }; i < samples.size(); ++i)
			maxStep = std::max(maxStep, std::abs(samples[i] - samples[i - 1]));
		EXPECT_LE(std::abs(samples.front() - samples.back()), maxStep * 1.01f + 1e-4f) << name;
	}
}

TEST(SfxSynth, GrindIsDarkerThanHiss)
{
	// Zero crossings per sample is a cheap brightness measure: hiss is broadband noise, while grind should
	// be a low, crunchy rumble.
	const auto crossing_rate{ [](const std::vector<float>& samples)
	{
		std::size_t crossings{};
		for (std::size_t i{ 1 }; i < samples.size(); ++i)
			crossings += (samples[i - 1] < 0.0f) != (samples[i] < 0.0f);
		return static_cast<float>(crossings) / static_cast<float>(samples.size());
	} };
	for (std::uint64_t seed{}; seed < 8; ++seed)
	{
		const auto grind{ synthesize(*generate_preset("grind", seed), { .loop = true, .seed = seed }) };
		const auto hiss{ synthesize(*generate_preset("hiss", seed), { .loop = true, .seed = seed }) };
		EXPECT_LT(crossing_rate(grind), crossing_rate(hiss) * 0.5f) << "seed " << seed;
	}
}

TEST(SfxSynth, MutateStaysInRangeAndKeepsWave)
{
	const auto base{ *generate_preset("laser", 3) };
	EXPECT_EQ(mutate(base, 0.0f, 1), base);

	const auto mutated{ mutate(base, 0.2f, 9) };
	EXPECT_NE(mutated, base);
	EXPECT_EQ(mutated.waveType, base.waveType);
	EXPECT_EQ(mutated.volume, base.volume);
	for (const auto& info : sfx_param_infos())
	{
		EXPECT_GE(mutated.*info.member, info.min) << info.name;
		EXPECT_LE(mutated.*info.member, info.max) << info.name;
		EXPECT_LE(std::abs(mutated.*info.member - base.*info.member), 0.2f + 1e-5f) << info.name;
	}
}

TEST(SfxSynth, WaveNamesAndClamping)
{
	for (std::size_t i{}; i < static_cast<std::size_t>(WaveType::Count); ++i)
	{
		const auto wave{ static_cast<WaveType>(i) };
		EXPECT_EQ(wave_type_from_name(wave_type_name(wave)), wave);
	}
	EXPECT_FALSE(wave_type_from_name("kazoo").has_value());

	SfxParams params{};
	params.baseFreq = 5.0f;
	params.freqSlide = -3.0f;
	params.decay = std::nanf("");
	clamp_params(params);
	EXPECT_EQ(params.baseFreq, 1.0f);
	EXPECT_EQ(params.freqSlide, -1.0f);
	EXPECT_EQ(params.decay, 0.0f);
}

TEST(AudioSystem, OneShotPlaysAndFinishes)
{
	auto audio{ make_offline_audio() };
	EXPECT_FALSE(audio.device_open());
	EXPECT_EQ(audio.sample_rate(), 48000u);
	ASSERT_TRUE(audio.define_sound("beep", simple_sound()));
	EXPECT_EQ(audio.play("missing"), 0u);

	const auto id{ audio.play("beep") };
	ASSERT_NE(id, 0u);
	EXPECT_TRUE(audio.is_playing(id));
	EXPECT_EQ(audio.voice_count(), 1u);

	const auto out{ mix_for(audio, 0.1f) };
	EXPECT_GT(channel_energy(out, 0), 1.0f);
	EXPECT_NEAR(channel_energy(out, 0), channel_energy(out, 1), 1e-3f);

	const auto length{ audio.sound_info("beep")->length };
	mix_for(audio, length + 0.1f);
	EXPECT_FALSE(audio.is_playing(id));
	EXPECT_EQ(audio.voice_count(), 0u);
	EXPECT_EQ(channel_energy(mix_for(audio, 0.05f), 0), 0.0f);
}

TEST(AudioSystem, PanAndBusVolumes)
{
	auto audio{ make_offline_audio() };
	ASSERT_TRUE(audio.define_sound("tone", simple_sound(true)));

	const auto id{ audio.play("tone", { .pan = -1.0f }) };
	ASSERT_NE(id, 0u);
	auto out{ mix_for(audio, 0.1f) };
	EXPECT_GT(channel_energy(out, 0), 1.0f);
	EXPECT_LT(channel_energy(out, 1), 1e-6f);

	ASSERT_TRUE(audio.update_voice(id, { .pan = 1.0f, .ramp = 0.0f }));
	out = mix_for(audio, 0.1f);
	EXPECT_LT(channel_energy(out, 0), 1e-6f);
	EXPECT_GT(channel_energy(out, 1), 1.0f);

	audio.set_bus_volume(Bus::Sfx, 0.0f);
	EXPECT_EQ(audio.bus_volume(Bus::Sfx), 0.0f);
	mix_for(audio, 0.05f);
	out = mix_for(audio, 0.1f);
	EXPECT_EQ(channel_energy(out, 1), 0.0f);

	audio.set_bus_volume(Bus::Sfx, 1.0f);
	audio.set_bus_volume(Bus::Master, 0.0f);
	mix_for(audio, 0.05f);
	EXPECT_EQ(channel_energy(mix_for(audio, 0.1f), 1), 0.0f);
	EXPECT_TRUE(audio.is_playing(id));
}

TEST(AudioSystem, LoopsKeepPlayingUntilStopped)
{
	auto audio{ make_offline_audio() };
	ASSERT_TRUE(audio.define_sound("hum", simple_sound(true)));
	const auto id{ audio.play("hum") };
	mix_for(audio, 2.0f);
	EXPECT_TRUE(audio.is_playing(id));
	EXPECT_GT(channel_energy(mix_for(audio, 0.1f), 0), 1.0f);

	audio.stop(id, 0.1f);
	EXPECT_FALSE(audio.is_playing(id));
	EXPECT_EQ(audio.voice_count(), 0u);
	mix_for(audio, 0.15f);
	EXPECT_EQ(channel_energy(mix_for(audio, 0.05f), 0), 0.0f);
}

TEST(AudioSystem, InstanceLimitsAndCooldowns)
{
	auto audio{ make_offline_audio() };
	auto def{ simple_sound(true) };
	def.maxInstances = 2;
	ASSERT_TRUE(audio.define_sound("limited", def));
	const auto first{ audio.play("limited") };
	audio.play("limited");
	audio.play("limited");
	EXPECT_EQ(audio.voice_count(), 2u);
	EXPECT_FALSE(audio.is_playing(first));

	def.cooldown = 30.0f;
	ASSERT_TRUE(audio.define_sound("cooldown", def));
	EXPECT_NE(audio.play("cooldown"), 0u);
	EXPECT_EQ(audio.play("cooldown"), 0u);

	audio.stop_sound("limited", 0.0f);
	EXPECT_EQ(audio.voice_count(), 1u);
	audio.stop_all(0.0f);
	EXPECT_EQ(audio.voice_count(), 0u);
}

TEST(AudioSystem, VariantsAreRenderedLazily)
{
	auto audio{ make_offline_audio() };
	auto def{ simple_sound() };
	def.params = *generate_preset("click", 1);
	def.variants = 5;
	def.mutation = 0.1f;
	def.pitchVariation = 2.0f;
	def.volumeVariation = 0.2f;
	ASSERT_TRUE(audio.define_sound("click", def));

	auto info{ audio.sound_info("click") };
	ASSERT_TRUE(info);
	EXPECT_EQ(info->variants, 5u);
	EXPECT_EQ(info->renderedVariants, 1u);

	ASSERT_TRUE(audio.prepare_sound("click"));
	EXPECT_EQ(audio.sound_info("click")->renderedVariants, 5u);

	// Redefining resets the variant cache.
	def.variants = 2;
	ASSERT_TRUE(audio.define_sound("click", def));
	EXPECT_EQ(audio.sound_info("click")->variants, 2u);
	EXPECT_EQ(audio.sound_names(), std::vector<std::string>{ "click" });
}

TEST(AudioSystem, ApplicationLoadsMusicAndBindsLua)
{
	testlog::LogCapture::instance().expect_total_error_count(1);
	ScopedEnv env{ "AXENG_AUDIO_BACKEND", "none" };

	const auto wavA{ make_wav(0.25f, 440.0f) };
	const auto wavB{ make_wav(0.25f, 660.0f) };
	const std::string manifest{
		"manifest = { name = 'axeng_test_audio', headless = true, audio = true, permissions = {}, "
		"scripts = { main = 'main.lua' }, entry_point = 'main', "
		"music = { track_a = 'music/a.wav', track_b = 'music/b.wav' } }"
	};
	const std::string entry_point{ R"(
		local audio = app.audio
		assert(audio ~= nil)
		assert(audio.available == false)
		assert(audio.sample_rate > 0)
		assert(#audio.presets() >= 10)
		local p1 = audio.generate('click', 3)
		local p2 = audio.generate('click', 3)
		assert(p1.wave ~= nil and p1.freq == p2.freq)
		assert(audio.generate('nope') == nil)
		local m = audio.mutate(p1, 0.1, 4)
		assert(m.wave == p1.wave)
		local infos = audio.params()
		assert(#infos == 23 and infos[1].name == 'attack' and infos[1].min == 0 and infos[1].max == 1)
		local found_slide = false
		for _, info in ipairs(infos) do
			assert(p1[info.name] ~= nil)
			if info.name == 'freq_slide' then found_slide = info.min == -1 end
		end
		assert(found_slide)
		assert(#audio.waves() == 5 and audio.waves()[1] == 'square')

		assert(audio.define('ui_click', { preset = 'click', seed = 1, mutation = 0.05, variants = 3, pitch_variation = 1, bus = 'ui' }))
		assert(audio.define('hum', { preset = 'hum', loop = true }))
		assert(audio.define('hum', { preset = 'hum', loop = true, volume = 0.5 }, { live = true }))
		assert(audio.define('custom', { params = { wave = 'sine', freq = 0.4, sustain = 0.1, decay = 0.1 } }))
		assert(not audio.define('bad', { preset = 'nope' }))
		assert(audio.has('custom') and not audio.has('bad'))
		local info = audio.info('ui_click')
		assert(info.variants == 3 and info.bus == 'ui' and info.length > 0)
		assert(info.peak > 0 and info.peak <= 1 and info.rms > 0 and info.rms <= info.peak)
		assert(audio.info('hum').loop)
		assert(audio.info('missing') == nil)
		assert(audio.prepare('ui_click'))
		assert(audio.info('ui_click').rendered_variants == 3)
		local wave = audio.waveform('ui_click', 32)
		assert(#wave.min == 32 and #wave.max == 32)
		local widest = 0
		for i = 1, 32 do
			assert(wave.min[i] <= 0 and wave.max[i] >= 0)
			widest = math.max(widest, wave.max[i] - wave.min[i])
		end
		assert(widest > 0.05)
		assert(audio.waveform('missing') == nil)

		local id = audio.play('ui_click')
		assert(id > 0 and audio.is_playing(id))
		assert(audio.play('missing') == 0)
		local loop = audio.play('hum', { volume = 0.5, pan = -0.5 })
		assert(audio.set(loop, { volume = 0.2, pan = 0.3, pitch = 1.1 }))
		assert(audio.is_playing(loop) and audio.voice_count() >= 1)
		audio.stop(loop, 0)
		assert(not audio.is_playing(loop))
		audio.stop_all(0)
		assert(audio.voice_count() == 0)

		assert(audio.set_volume('music', 0.5))
		assert(math.abs(audio.get_volume('music') - 0.5) < 1e-6)
		assert(audio.set_volume('music', 1))
		assert(not audio.set_volume('nope', 1))
		assert(audio.get_volume('nope') == nil)

		local tracks = audio.music.list()
		assert(#tracks == 2)
		assert(math.abs(audio.music.info('track_a').length - 0.25) < 0.01)
		assert(audio.music.info('missing') == nil)
		assert(audio.music.play('track_a', { fade = 0 }))
		assert(audio.music.current() == 'track_a')
		assert(audio.music.playing())
		audio.music.pause()
		assert(audio.music.paused())
		audio.music.resume()
		audio.music.stop(0)
		assert(audio.music.current() == nil)
		assert(not audio.music.play('missing'))
		return 0
	)" };

	ax::EmbeddedResourceLayout layout;
	layout.emplace("manifest.luac", ax::EmbeddedResource{ reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size() });
	layout.emplace("main.lua", ax::EmbeddedResource{ reinterpret_cast<const uint8_t*>(entry_point.data()), entry_point.size() });
	layout.emplace("music/a.wav", ax::EmbeddedResource{ wavA.data(), wavA.size() });
	layout.emplace("music/b.wav", ax::EmbeddedResource{ wavB.data(), wavB.size() });

	auto app{ ax::Application::from_embedded({}, std::move(layout)) };
	ASSERT_TRUE(app.try_load({}, wgpu::BackendType::Undefined));

	auto* const audio{ app.audio() };
	ASSERT_NE(audio, nullptr);
	const auto* a{ app.music().get("track_a") };
	const auto* b{ app.music().get("track_b") };
	ASSERT_TRUE(a && b);
	EXPECT_EQ(a->sample_rate(), 22050u);
	EXPECT_EQ(a->channels(), 1u);

	// A shuffled playlist never plays the same track twice in a row.
	ASSERT_TRUE(audio->play_playlist({ a, b }, { .shuffle = true, .repeat = true, .fade = 0.0f, .gap = 0.0f }));
	std::vector<std::string> played{ audio->current_music() };
	for (int i{}; i < 20; ++i)
	{
		mix_for(*audio, 0.05f);
		const auto current{ audio->current_music() };
		if (!current.empty() && current != played.back())
			played.push_back(current);
	}
	ASSERT_GE(played.size(), 3u);
	for (std::size_t i{ 1 }; i < played.size(); ++i)
		EXPECT_NE(played[i], played[i - 1]);

	const auto out{ mix_for(*audio, 0.05f) };
	EXPECT_GT(channel_energy(out, 0), 0.0f);

	ASSERT_TRUE(audio->next_track(0.0f));
	audio->stop_music(0.0f);
	EXPECT_FALSE(audio->playlist_active());
	mix_for(*audio, 0.01f);
	EXPECT_TRUE(audio->current_music().empty());
}

TEST(AudioSystem, LiveRedefinitionUpdatesPlayingLoops)
{
	auto audio{ make_offline_audio() };
	auto def{ simple_sound(true) };
	ASSERT_TRUE(audio.define_sound("hum", def));
	const auto id{ audio.play("hum") };
	mix_for(audio, 0.2f);
	const auto before{ channel_energy(mix_for(audio, 0.2f), 0) };
	ASSERT_GT(before, 1.0f);

	// A live redefinition keeps the same voice playing, now at the new level.
	def.volume = 0.5f;
	def.params.baseFreq = 0.5f;
	ASSERT_TRUE(audio.define_sound("hum", def, true));
	EXPECT_TRUE(audio.is_playing(id));
	mix_for(audio, 0.1f);
	const auto after{ channel_energy(mix_for(audio, 0.2f), 0) };
	EXPECT_TRUE(audio.is_playing(id));
	EXPECT_NEAR(after / before, 0.25f, 0.05f);

	// Turning looping off lets the voice finish instead of cutting it.
	def.loop = false;
	ASSERT_TRUE(audio.define_sound("hum", def, true));
	EXPECT_TRUE(audio.is_playing(id));
	mix_for(audio, 2.5f);
	EXPECT_FALSE(audio.is_playing(id));

	// Without `live`, a redefinition doesn't touch playing voices.
	def.loop = true;
	ASSERT_TRUE(audio.define_sound("hum", def));
	const auto other{ audio.play("hum") };
	def.volume = 0.0f;
	ASSERT_TRUE(audio.define_sound("hum", def));
	EXPECT_GT(channel_energy(mix_for(audio, 0.1f), 0), 0.1f);
	EXPECT_TRUE(audio.is_playing(other));
}

TEST(AudioSystem, UpdatingVolumeKeepsTheSoundsLevel)
{
	auto audio{ make_offline_audio() };
	auto def{ simple_sound(true) };
	def.volume = 0.25f;
	ASSERT_TRUE(audio.define_sound("quiet", def));
	const auto id{ audio.play("quiet", { .volume = 1.0f }) };
	mix_for(audio, 0.1f);
	const auto before{ channel_energy(mix_for(audio, 0.2f), 0) };
	ASSERT_TRUE(audio.update_voice(id, { .volume = 1.0f, .ramp = 0.0f }));
	const auto same{ channel_energy(mix_for(audio, 0.2f), 0) };
	EXPECT_NEAR(same / before, 1.0f, 0.05f);
	ASSERT_TRUE(audio.update_voice(id, { .volume = 0.5f, .ramp = 0.0f }));
	const auto half{ channel_energy(mix_for(audio, 0.2f), 0) };
	EXPECT_NEAR(half / before, 0.25f, 0.03f);
}

TEST(AudioSystem, TimePassesWithoutADevice)
{
	// Without a device the system mixes elapsed time itself, so sounds still finish and get cleaned up.
	AudioSystem audio{ AudioSystem::Config{ .openDevice = false } };
	EXPECT_FALSE(audio.device_open());
	ASSERT_TRUE(audio.define_sound("beep", simple_sound()));
	ASSERT_TRUE(audio.define_sound("hum", simple_sound(true)));
	const auto beep{ audio.play("beep") };
	const auto hum{ audio.play("hum") };
	EXPECT_TRUE(audio.is_playing(beep));
	audio.stop(hum, 0.01f);

	const auto length{ audio.sound_info("beep")->length };
	std::this_thread::sleep_for(std::chrono::duration<float>{ length + 0.1f });
	EXPECT_FALSE(audio.is_playing(beep));
	EXPECT_FALSE(audio.is_playing(hum));
	EXPECT_EQ(audio.voice_count(), 0u);
}

TEST(AudioSystem, OutputDeviceRunsSilently)
{
	// Opens the real output device (or the null backend if AXENG_AUDIO_BACKEND=null) but plays nothing.
	auto config{ AudioSystem::config_from_environment() };
	if (!config.openDevice)
		GTEST_SKIP() << "Audio disabled by AXENG_AUDIO_BACKEND";
	AudioSystem audio{ config };
	if (!audio.device_open())
		GTEST_SKIP() << "No audio output device available";
	EXPECT_GT(audio.sample_rate(), 0u);
	auto silent{ simple_sound() };
	silent.volume = 0.0f;
	ASSERT_TRUE(audio.define_sound("silent", silent));
	const auto id{ audio.play("silent") };
	EXPECT_TRUE(audio.is_playing(id));
	// Give the device thread time to mix the sound to completion.
	for (int i{}; i < 100 && audio.is_playing(id); ++i)
		std::this_thread::sleep_for(std::chrono::milliseconds{ 10 });
	EXPECT_FALSE(audio.is_playing(id));
}

TEST(AudioSystem, HeadlessAppsHaveNoAudioByDefault)
{
	const std::string manifest{
		"manifest = { name = 'axeng_test_no_audio', headless = true, permissions = {}, "
		"scripts = { main = 'main.lua' }, entry_point = 'main' }"
	};
	const std::string entry_point{ "assert(app.audio == nil)\nreturn 0" };
	ax::EmbeddedResourceLayout layout;
	layout.emplace("manifest.luac", ax::EmbeddedResource{ reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size() });
	layout.emplace("main.lua", ax::EmbeddedResource{ reinterpret_cast<const uint8_t*>(entry_point.data()), entry_point.size() });

	auto app{ ax::Application::from_embedded({}, std::move(layout)) };
	ASSERT_TRUE(app.try_load({}, wgpu::BackendType::Undefined));
	EXPECT_EQ(app.audio(), nullptr);
}

TEST(AudioSystem, InvalidMusicFailsGracefully)
{
	testlog::LogCapture::instance().expect_total_error_count(2);
	ScopedEnv env{ "AXENG_AUDIO_BACKEND", "none" };

	const std::string bogus{ "definitely not audio" };
	const std::string manifest{
		"manifest = { name = 'axeng_test_bad_music', headless = true, audio = true, permissions = {}, "
		"scripts = { main = 'main.lua' }, entry_point = 'main', music = { bad = 'bad.ogg' } }"
	};
	const std::string entry_point{ "assert(#app.audio.music.list() == 0)\nreturn 0" };
	ax::EmbeddedResourceLayout layout;
	layout.emplace("manifest.luac", ax::EmbeddedResource{ reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size() });
	layout.emplace("main.lua", ax::EmbeddedResource{ reinterpret_cast<const uint8_t*>(entry_point.data()), entry_point.size() });
	layout.emplace("bad.ogg", ax::EmbeddedResource{ reinterpret_cast<const uint8_t*>(bogus.data()), bogus.size() });

	auto app{ ax::Application::from_embedded({}, std::move(layout)) };
	ASSERT_TRUE(app.try_load({}, wgpu::BackendType::Undefined));
}
