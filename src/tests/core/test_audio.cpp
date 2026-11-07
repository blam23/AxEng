#include <gtest/gtest.h>

#include "axeng/core/application.h"
#include "axeng/core/audio/audio_system.h"
#include "axeng/core/audio/instrument.h"
#include "axeng/core/audio/sfx_synth.h"
#include "tests/main/log_capture.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <span>
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

	Arrangement simple_arrangement()
	{
		SynthInstrumentDefinition synth{};
		synth.gain = 1.0f;
		synth.lowPassHz = 24000.0f;
		synth.envelope = { .attack = 0.0f, .decay = 0.0f, .sustain = 1.0f, .release = 0.01f };
		return Arrangement{
			.tempo = 60.0f,
			.durationBeats = 4.0f,
			.instruments = { ArrangementInstrument{ .definition = synth } },
			.notes = { ArrangementNote{ .startBeat = 1.0f, .durationBeats = 2.0f,
				.instrument = 0, .note = 69, .velocity = 1.0f, .pan = 0.0f } } };
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

TEST(InstrumentVoice, SynthUsesMidiPitchAndReleaseEnvelope)
{
	SynthInstrumentDefinition definition{};
	definition.wave = InstrumentWave::Sine;
	definition.gain = 1.0f;
	definition.envelope = { .attack = 0.0f, .decay = 0.0f, .sustain = 1.0f, .release = 0.01f };
	SynthInstrumentVoice voice{ definition, 69, 1.0f, 8000 };
	ASSERT_TRUE(voice.valid());

	std::vector<float> samples{};
	float sample{};
	for (int i{}; i < 800; ++i)
	{
		ASSERT_TRUE(voice.next(sample));
		samples.push_back(sample);
	}

	std::size_t crossings{};
	for (std::size_t i{ 1 }; i < samples.size(); ++i)
		crossings += (samples[i - 1] < 0.0f) != (samples[i] < 0.0f);
	EXPECT_NEAR(static_cast<double>(crossings) / 2.0, 440.0 * 0.1, 1.0);
	EXPECT_GT(peak(samples), 0.9f);

	voice.note_off();
	for (int i{}; i < 80; ++i)
		ASSERT_TRUE(voice.next(sample));
	EXPECT_FALSE(voice.next(sample));
	EXPECT_TRUE(voice.finished());
	EXPECT_FLOAT_EQ(sample, 0.0f);
}

TEST(InstrumentVoice, NoiseAndPitchDropSupportElectronicPercussion)
{
	SynthInstrumentDefinition noise{};
	noise.wave = InstrumentWave::Noise;
	noise.gain = 1.0f;
	noise.envelope = { .attack = 0.0f, .decay = 0.0f, .sustain = 1.0f, .release = 0.01f };
	SynthInstrumentVoice noiseVoice{ noise, 42, 1.0f, 8000 };
	ASSERT_TRUE(noiseVoice.valid());
	std::vector<float> noiseSamples{};
	float sample{};
	for (int i{}; i < 128; ++i)
	{
		ASSERT_TRUE(noiseVoice.next(sample));
		noiseSamples.push_back(sample);
	}
	EXPECT_GT(peak(noiseSamples), 0.9f);
	EXPECT_LT(*std::min_element(noiseSamples.begin(), noiseSamples.end()), -0.9f);
	EXPECT_NE(noiseSamples[0], noiseSamples[1]);

	SynthInstrumentDefinition kick{};
	kick.wave = InstrumentWave::Sine;
	kick.gain = 1.0f;
	kick.pitchDropSemitones = 24.0f;
	kick.pitchDropSeconds = 0.1f;
	kick.envelope = { .attack = 0.0f, .decay = 0.0f, .sustain = 1.0f, .release = 0.01f };
	SynthInstrumentVoice kickVoice{ kick, 69, 1.0f, 8000 };
	ASSERT_TRUE(kickVoice.valid());
	std::vector<float> kickSamples{};
	for (int i{}; i < 1600; ++i)
	{
		ASSERT_TRUE(kickVoice.next(sample));
		kickSamples.push_back(sample);
	}
	const auto crossings{ [](std::span<const float> values)
	{
		std::size_t count{};
		for (std::size_t i{ 1 }; i < values.size(); ++i)
			count += (values[i - 1] < 0.0f) != (values[i] < 0.0f);
		return count;
	} };
	const auto earlyCrossings{ crossings(std::span{ kickSamples }.subspan(0, 160)) };
	const auto lateCrossings{ crossings(std::span{ kickSamples }.subspan(960, 160)) };
	EXPECT_GT(earlyCrossings, lateCrossings * 3);
}

TEST(InstrumentVoice, SynthSampleRenderIsDeterministicBoundedAndRoundTrips)
{
	SynthInstrumentDefinition definition{};
	definition.wave = InstrumentWave::Triangle;
	definition.gain = 1.5f;
	definition.vibratoCents = 12.0f;
	definition.vibratoRate = 4.0f;
	definition.envelope = { .attack = 0.02f, .decay = 0.1f, .sustain = 0.7f, .release = 0.15f };
	std::string error{};

	const auto first{ render_synth_sample(definition, 60, 0.8f, 0.25f, 22050, error) };
	ASSERT_TRUE(first) << error;
	const auto second{ render_synth_sample(definition, 60, 0.8f, 0.25f, 22050, error) };
	ASSERT_TRUE(second) << error;
	EXPECT_EQ(first->samples, second->samples);
	EXPECT_EQ(first->sampleRate, 22050u);
	EXPECT_EQ(first->channels, 1u);
	EXPECT_EQ(first->frame_count(), 8821u);
	EXPECT_LE(peak(first->samples), 0.95f);
	EXPECT_GT(peak(first->samples), 0.1f);

	const auto wav{ ax::AudioSample::encode_wav(*first, error) };
	ASSERT_TRUE(wav) << error;
	const auto decoded{ decode_audio_sample(*wav) };
	ASSERT_TRUE(decoded);
	EXPECT_EQ(decoded->sampleRate, first->sampleRate);
	EXPECT_EQ(decoded->channels, first->channels);
	EXPECT_EQ(decoded->frame_count(), first->frame_count());
	EXPECT_NEAR(peak(decoded->samples), peak(first->samples), 0.001f);
}

TEST(InstrumentVoice, SynthSampleRenderRejectsInvalidAndOversizedRequests)
{
	std::string error{};
	EXPECT_FALSE(render_synth_sample(SynthInstrumentDefinition{}, 128, 1.0f, 1.0f, 48000, error));
	EXPECT_FALSE(error.empty());
	EXPECT_FALSE(render_synth_sample(SynthInstrumentDefinition{}, 60, 1.0f,
		synth_sample_max_seconds, 48000, error));
	EXPECT_FALSE(error.empty());
	EXPECT_FALSE(render_synth_sample(SynthInstrumentDefinition{}, 60, 1.0f, 1.0f, 192000, error));
	EXPECT_FALSE(error.empty());
}

TEST(InstrumentVoice, RejectsInvalidSynthSettings)
{
	SynthInstrumentDefinition definition{};
	definition.gain = std::numeric_limits<float>::infinity();
	const SynthInstrumentVoice invalidGain{ definition, 60, 1.0f, 48000 };
	const SynthInstrumentVoice invalidNote{ SynthInstrumentDefinition{}, 128, 1.0f, 48000 };
	const SynthInstrumentVoice invalidRate{ SynthInstrumentDefinition{}, 60, 1.0f, 0 };
	EXPECT_FALSE(invalidGain.valid());
	EXPECT_FALSE(invalidNote.valid());
	EXPECT_FALSE(invalidRate.valid());
}

TEST(InstrumentVoice, DecodesEncodedSamples)
{
	const auto encoded{ make_wav(0.1f, 440.0f) };
	const auto sample{ decode_audio_sample(encoded) };
	ASSERT_TRUE(sample.has_value());
	EXPECT_EQ(sample->sampleRate, 22050u);
	EXPECT_EQ(sample->channels, 1u);
	EXPECT_EQ(sample->frame_count(), 2205u);
	EXPECT_TRUE(sample->valid());
	EXPECT_FALSE(decode_audio_sample(std::span<const std::uint8_t>{}).has_value());
	EXPECT_FALSE(decode_audio_sample(std::span<const std::uint8_t>{ encoded.data(), 16 }).has_value());
}

TEST(InstrumentVoice, SampleMetadataValidationDoesNotScanPcm)
{
	AudioSampleData sample{};
	sample.sampleRate = 8000;
	sample.channels = 1;
	sample.samples = { 0.0f, std::numeric_limits<float>::quiet_NaN() };

	EXPECT_TRUE(sample.metadata_valid());
	EXPECT_FALSE(sample.valid());
}

TEST(AudioSampleManager, LoadsSamplesFromTheProjectManifest)
{
	const auto wav{ make_wav(0.1f, 440.0f) };
	const std::string manifest{
		"manifest = { name = 'axeng_test_audio_samples', headless = true, audio = true, permissions = {}, "
		"scripts = { main = 'main.lua' }, entry_point = 'main', "
		"audio_samples = { piano = 'piano.wav' } }"
	};
	const std::string entryPoint{ "return 0" };
	ax::EmbeddedResourceLayout layout;
	layout.emplace("manifest.luac", ax::EmbeddedResource{ reinterpret_cast<const std::uint8_t*>(manifest.data()), manifest.size() });
	layout.emplace("main.lua", ax::EmbeddedResource{ reinterpret_cast<const std::uint8_t*>(entryPoint.data()), entryPoint.size() });
	layout.emplace("piano.wav", ax::EmbeddedResource{ wav.data(), wav.size() });

	auto app{ ax::Application::from_embedded({}, std::move(layout)) };
	app.set_audio_config(AudioSystem::Config{ .openDevice = false });
	ASSERT_TRUE(app.try_load({}, wgpu::BackendType::Undefined));
	ASSERT_NE(app.audio(), nullptr);

	const auto* sample{ app.audio_samples().get("piano") };
	ASSERT_NE(sample, nullptr);
	EXPECT_TRUE(sample->is_loaded());
	EXPECT_EQ(sample->pcm().sampleRate, 22050u);
	EXPECT_EQ(sample->pcm().channels, 1u);
	EXPECT_EQ(sample->pcm().frame_count(), 2205u);
}

TEST(AudioSampleManager, InvalidSamplesReportOneLoadError)
{
	testlog::LogCapture::instance().expect_total_error_count(1);

	const std::string manifest{
		"manifest = { name = 'axeng_test_bad_audio_sample', headless = true, audio = true, permissions = {}, "
		"scripts = { main = 'main.lua' }, entry_point = 'main', "
		"audio_samples = { broken = 'broken.wav' } }"
	};
	const std::string entry_point{ "return 0" };
	const std::string invalid_sample{ "not an audio file" };
	ax::EmbeddedResourceLayout layout;
	layout.emplace("manifest.luac", ax::EmbeddedResource{ reinterpret_cast<const std::uint8_t*>(manifest.data()), manifest.size() });
	layout.emplace("main.lua", ax::EmbeddedResource{ reinterpret_cast<const std::uint8_t*>(entry_point.data()), entry_point.size() });
	layout.emplace("broken.wav", ax::EmbeddedResource{ reinterpret_cast<const std::uint8_t*>(invalid_sample.data()), invalid_sample.size() });

	auto app{ ax::Application::from_embedded({}, std::move(layout)) };
	app.set_audio_config(AudioSystem::Config{ .openDevice = false });
	ASSERT_TRUE(app.try_load({}, wgpu::BackendType::Undefined));
	EXPECT_EQ(app.audio_samples().get("broken"), nullptr);
}

TEST(AudioSystem, LuaCanPlaySynthAndSamplerInstruments)
{
	const auto wav{ make_wav(0.1f, 440.0f) };
	const std::string manifest{
		"manifest = { name = 'axeng_test_pitched_audio', headless = true, audio = true, permissions = {}, "
		"scripts = { main = 'main.lua' }, entry_point = 'main', "
		"audio_samples = { piano = 'piano.wav' } }"
	};
	const std::string entry_point{
		"local env = { attack = 0, decay = 0, sustain = 1, release = 0.01 }\n"
		"local synth = app.audio.play_synth({ wave = 'sine', gain = 1, envelope = env }, 69)\n"
		"assert(synth > 0 and app.audio.note_off(synth))\n"
		"local sample = app.audio.play_sampler('piano', { root_note = 60, end_frame = 2205, envelope = env }, 60)\n"
		"assert(sample > 0 and app.audio.note_off(sample))\n"
		"local rendered_wav = app.audio.render_synth_sample({ wave = 'triangle', gain = 1, envelope = env }, 60, "
		"{ hold_seconds = 0.1, sample_rate = 22050 })\n"
		"assert(type(rendered_wav) == 'string' and #rendered_wav > 44)\n"
		"local inspected_sample = app.audio.inspect_sample(rendered_wav)\n"
		"assert(inspected_sample.sample_rate == 22050 and inspected_sample.channels == 1)\n"
		"assert(inspected_sample.frames > 0 and inspected_sample.duration_seconds > 0)\n"
		"assert(app.audio.sample_info('rendered') == nil)\n"
		"assert(app.audio.load_sample('rendered', rendered_wav))\n"
		"local sample_info = app.audio.sample_info('rendered')\n"
		"assert(sample_info.sample_rate == 22050 and sample_info.channels == 1 and sample_info.frames > 0)\n"
		"assert(sample_info.duration_seconds > 0 and app.audio.sample_info('missing') == nil)\n"
		"local rendered_voice = app.audio.play_sampler('rendered', { root_note = 60, envelope = env }, 60)\n"
		"assert(rendered_voice > 0 and app.audio.note_off(rendered_voice))\n"
		"assert(app.audio.sequence.define('ambient', {\n"
		"  tempo = 60, duration_beats = 4,\n"
		"  reverb = { wet = 0.25, decay_seconds = 3, damping = 0.35 },\n"
		"  instruments = {{ type = 'synth', wave = 'sine', gain = 1, envelope = env }},\n"
		"  notes = {{ start_beat = 0, duration_beats = 2, instrument = 1, note = 60 }}\n"
		"}))\n"
		"assert(app.audio.sequence.play('ambient', 0))\n"
		"local state = app.audio.sequence.state()\n"
		"assert(state.name == 'ambient' and state.active and state.duration_seconds == 4)\n"
		"assert(app.audio.sequence.seek(1))\n"
		"assert(app.audio.sequence.state().position_seconds == 1)\n"
		"return 0"
	};
	ax::EmbeddedResourceLayout layout;
	layout.emplace("manifest.luac", ax::EmbeddedResource{ reinterpret_cast<const std::uint8_t*>(manifest.data()), manifest.size() });
	layout.emplace("main.lua", ax::EmbeddedResource{ reinterpret_cast<const std::uint8_t*>(entry_point.data()), entry_point.size() });
	layout.emplace("piano.wav", ax::EmbeddedResource{ wav.data(), wav.size() });

	auto app{ ax::Application::from_embedded({}, std::move(layout)) };
	app.set_audio_config(AudioSystem::Config{ .openDevice = false });
	ASSERT_TRUE(app.try_load({}, wgpu::BackendType::Undefined));
	EXPECT_EQ(app.audio()->voice_count(), 3u);
	ASSERT_NE(app.audio_samples().get("rendered"), nullptr);
	EXPECT_EQ(app.audio_samples().get("rendered")->pcm().sampleRate, 22050u);
}

namespace
{
	FmInstrumentDefinition fm_tone()
	{
		FmInstrumentDefinition definition{};
		definition.algorithm = 32;
		definition.gain = 0.5f;
		definition.envelope.release = 0.01f;
		for (auto& op : definition.operators)
			op.outputLevel = 0;
		definition.operators[0].outputLevel = 99;
		return definition;
	}

	Arrangement fm_arrangement(float chorusSend)
	{
		return Arrangement{
			.tempo = 60.0f,
			.durationBeats = 4.0f,
			.instruments = { ArrangementInstrument{ .definition = fm_tone(), .chorusSend = chorusSend } },
			.notes = { ArrangementNote{ .startBeat = 0.0f, .durationBeats = 3.0f,
				.instrument = 0, .note = 69, .velocity = 1.0f, .pan = 0.0f } } };
	}

	float channel_difference(const std::vector<float>& stereo)
	{
		float result{};
		for (std::size_t i{}; i + 1 < stereo.size(); i += 2)
			result = std::max(result, std::abs(stereo[i] - stereo[i + 1]));
		return result;
	}
}

TEST(AudioSystem, PlaysFmVoices)
{
	auto audio{ make_offline_audio() };
	const auto id{ audio.play_fm(fm_tone(), 69, 1.0f, { .bus = Bus::Music }) };
	ASSERT_NE(id, 0u);
	EXPECT_GT(peak(mix_for(audio, 0.1f)), 0.1f);
	EXPECT_TRUE(audio.note_off(id));
	mix_for(audio, 0.1f);
	EXPECT_EQ(audio.voice_count(), 0u);

	auto invalid{ fm_tone() };
	invalid.algorithm = 0;
	EXPECT_EQ(audio.play_fm(invalid, 69), 0u);
}

TEST(Arrangement, FmInstrumentsPlayAndSeek)
{
	auto uninterrupted{ make_offline_audio() };
	auto sought{ make_offline_audio() };
	std::string error{};
	ASSERT_TRUE(uninterrupted.define_arrangement("test", fm_arrangement(0.0f), error)) << error;
	ASSERT_TRUE(sought.define_arrangement("test", fm_arrangement(0.0f), error)) << error;
	EXPECT_TRUE(uninterrupted.play_arrangement("test", 0.0f));
	EXPECT_TRUE(sought.play_arrangement("test", 0.0f));

	EXPECT_GT(peak(mix_for(uninterrupted, 1.5f)), 0.1f);
	ASSERT_TRUE(sought.seek_arrangement(1.5f));
	const auto expected{ mix_for(uninterrupted, 0.1f) };
	const auto actual{ mix_for(sought, 0.1f) };
	ASSERT_EQ(expected.size(), actual.size());
	// The voice's DC filter restarts on a seek, and for a moment lacks the small phase lead it has when settled.
	for (std::size_t i{}; i < expected.size(); ++i)
		EXPECT_NEAR(expected[i], actual[i], 0.03f);
}

TEST(Arrangement, RejectsInvalidFmInstrumentsAndChorusSends)
{
	std::string error{};
	auto arrangement{ fm_arrangement(0.0f) };
	std::get<FmInstrumentDefinition>(arrangement.instruments[0].definition).feedback = 8;
	EXPECT_EQ(prepare_arrangement(std::move(arrangement), 48000, error), nullptr);
	EXPECT_EQ(prepare_arrangement(fm_arrangement(1.5f), 48000, error), nullptr);
}

TEST(Arrangement, ChorusWidensACentredSoundAtTheSameLevel)
{
	const auto render{ [](float send)
	{
		auto audio{ make_offline_audio() };
		std::string error{};
		EXPECT_TRUE(audio.define_arrangement("test", fm_arrangement(send), error)) << error;
		EXPECT_TRUE(audio.play_arrangement("test", 0.0f));
		return mix_for(audio, 1.0f);
	} };
	const auto dry{ render(0.0f) };
	const auto wide{ render(1.0f) };
	EXPECT_LT(channel_difference(dry), 1e-6f);
	EXPECT_GT(channel_difference(wide), 0.05f);
	EXPECT_NEAR(channel_energy(wide, 0) / channel_energy(dry, 0), 0.75f, 0.35f);
	EXPECT_LE(peak(wide), peak(dry) * 1.05f);
}

TEST(Arrangement, ReverbSendKeepsAnInstrumentDry)
{
	const auto render{ [](float send)
	{
		auto arrangement{ fm_arrangement(0.0f) };
		arrangement.reverb = { .wet = 0.5f, .decaySeconds = 2.0f, .damping = 0.3f };
		arrangement.instruments[0].reverbSend = send;
		auto audio{ make_offline_audio() };
		std::string error{};
		EXPECT_TRUE(audio.define_arrangement("test", std::move(arrangement), error)) << error;
		EXPECT_TRUE(audio.play_arrangement("test", 0.0f));
		mix_for(audio, 3.1f);
		return peak(mix_for(audio, 0.5f));
	} };
	EXPECT_GT(render(1.0f), 0.01f);
	EXPECT_LT(render(0.0f), 1e-6f);

	std::string error{};
	auto invalid{ fm_arrangement(0.0f) };
	invalid.instruments[0].reverbSend = 1.5f;
	EXPECT_EQ(prepare_arrangement(std::move(invalid), 48000, error), nullptr);
}

TEST(Arrangement, SeekClearsTheChorus)
{
	auto audio{ make_offline_audio() };
	std::string error{};
	ASSERT_TRUE(audio.define_arrangement("test", fm_arrangement(1.0f), error)) << error;
	ASSERT_TRUE(audio.play_arrangement("test", 0.0f));
	mix_for(audio, 0.5f);
	ASSERT_TRUE(audio.seek_arrangement(3.9f));
	EXPECT_LT(peak(mix_for(audio, 0.01f)), 1e-6f);
}

TEST(AudioSystem, LuaCanPlayFmInstruments)
{
	const std::string manifest{
		"manifest = { name = 'axeng_test_fm_audio', headless = true, audio = true, permissions = {}, "
		"scripts = { main = 'main.lua' }, entry_point = 'main' }"
	};
	const std::string entry_point{
		"local op = { output_level = 99, coarse = 2, fine = 10, detune = -3, fixed = false,\n"
		"  rates = { 99, 40, 30, 50 }, levels = { 99, 70, 0, 0 }, velocity_sensitivity = 3, amp_mod_sensitivity = 1,\n"
		"  rate_scaling = 2, breakpoint = 39, left_depth = 10, right_depth = 20,\n"
		"  left_curve = 'neg_exp', right_curve = 'pos_lin' }\n"
		"local patch = { algorithm = 5, feedback = 4, transpose = -12, gain = 0.5,\n"
		"  envelope = { attack = 0, decay = 0, sustain = 1, release = 0.01 },\n"
		"  pitch_envelope = { rates = { 99, 99, 99, 99 }, levels = { 50, 50, 50, 50 } },\n"
		"  lfo = { wave = 'sample_hold', speed = 30, delay = 10, pitch_depth = 4, amp_depth = 2, pitch_sensitivity = 3 },\n"
		"  operators = { op, op, op, op, op, op } }\n"
		"local routing = app.audio.fm_algorithm(4)\n"
		"assert(#routing.modulators == 6 and routing.modulators[1][1] == 2 and #routing.carriers == 2)\n"
		"assert(routing.feedback_target == 6 and routing.feedback_source == 4 and app.audio.fm_algorithm(33) == nil)\n"
		"local voice = app.audio.play_fm(patch, 60, { velocity = 0.8, bus = 'music' })\n"
		"assert(voice > 0 and app.audio.note_off(voice))\n"
		"local wav = app.audio.render_fm_sample(patch, 60, { hold_seconds = 0.1, sample_rate = 22050 })\n"
		"assert(type(wav) == 'string' and app.audio.inspect_sample(wav).sample_rate == 22050)\n"
		"local instrument = { type = 'fm', chorus_send = 0.5, reverb_send = 0.25 }\n"
		"for key, value in pairs(patch) do instrument[key] = value end\n"
		"assert(app.audio.sequence.define('fm', { tempo = 60, duration_beats = 4, instruments = { instrument },\n"
		"  notes = {{ start_beat = 0, duration_beats = 2, instrument = 1, note = 60 }} }))\n"
		"assert(app.audio.sequence.play('fm', 0))\n"
		"return 0"
	};
	ax::EmbeddedResourceLayout layout;
	layout.emplace("manifest.luac", ax::EmbeddedResource{ reinterpret_cast<const std::uint8_t*>(manifest.data()), manifest.size() });
	layout.emplace("main.lua", ax::EmbeddedResource{ reinterpret_cast<const std::uint8_t*>(entry_point.data()), entry_point.size() });

	auto app{ ax::Application::from_embedded({}, std::move(layout)) };
	app.set_audio_config(AudioSystem::Config{ .openDevice = false });
	ASSERT_TRUE(app.try_load({}, wgpu::BackendType::Undefined));
	EXPECT_EQ(app.audio()->voice_count(), 1u);
}

TEST(AudioSystem, LuaRejectsBadFmFields)
{
	testlog::LogCapture::instance().expect_total_error_count(3);
	const std::string manifest{
		"manifest = { name = 'axeng_test_bad_fm', headless = true, audio = true, permissions = {}, "
		"scripts = { main = 'main.lua' }, entry_point = 'main' }"
	};
	const std::string entry_point{
		"local op = {}\n"
		"assert(app.audio.play_fm({ operators = { op, op, op } }, 60) == 0)\n"
		"assert(app.audio.play_fm({ algorithm = 33, operators = { op, op, op, op, op, op } }, 60) == 0)\n"
		"assert(app.audio.play_fm({ operators = { { left_curve = 'wobbly' }, op, op, op, op, op } }, 60) == 0)\n"
		"return 0"
	};
	ax::EmbeddedResourceLayout layout;
	layout.emplace("manifest.luac", ax::EmbeddedResource{ reinterpret_cast<const std::uint8_t*>(manifest.data()), manifest.size() });
	layout.emplace("main.lua", ax::EmbeddedResource{ reinterpret_cast<const std::uint8_t*>(entry_point.data()), entry_point.size() });
	auto app{ ax::Application::from_embedded({}, std::move(layout)) };
	app.set_audio_config(AudioSystem::Config{ .openDevice = false });
	ASSERT_TRUE(app.try_load({}, wgpu::BackendType::Undefined));
}

TEST(AudioSystem, ArrangementSchedulesSeeksPausesAndResumes)
{
	auto uninterrupted{ make_offline_audio() };
	auto sought{ make_offline_audio() };
	std::string error{};
	ASSERT_TRUE(uninterrupted.define_arrangement("test", simple_arrangement(), error)) << error;
	ASSERT_TRUE(sought.define_arrangement("test", simple_arrangement(), error)) << error;
	EXPECT_TRUE(uninterrupted.play_arrangement("test", 0.0f));
	EXPECT_TRUE(sought.play_arrangement("test", 0.0f));

	const auto silence{ mix_for(uninterrupted, 1.0f) };
	EXPECT_LT(peak(silence), 1e-6f);
	mix_for(uninterrupted, 0.5f);
	ASSERT_TRUE(sought.seek_arrangement(1.5f));
	const auto uninterruptedAudio{ mix_for(uninterrupted, 0.1f) };
	const auto soughtAudio{ mix_for(sought, 0.1f) };
	ASSERT_EQ(uninterruptedAudio.size(), soughtAudio.size());
	for (std::size_t i{}; i < uninterruptedAudio.size(); ++i)
		EXPECT_NEAR(uninterruptedAudio[i], soughtAudio[i], 1e-5f);

	const auto beforePause{ sought.arrangement_playback().positionSeconds };
	sought.set_arrangement_paused(true);
	mix_for(sought, 0.25f);
	EXPECT_FLOAT_EQ(sought.arrangement_playback().positionSeconds, beforePause);
	sought.set_arrangement_paused(false);
	mix_for(sought, 0.1f);
	EXPECT_GT(sought.arrangement_playback().positionSeconds, beforePause);
	EXPECT_TRUE(sought.arrangement_playback().active);

	EXPECT_TRUE(sought.seek_arrangement(4.0f));
	EXPECT_FALSE(sought.arrangement_playback().active);
}

TEST(AudioSystem, ArrangementPlaybackIsIndependentOfEncodedMusicPause)
{
	auto audio{ make_offline_audio() };
	std::string error{};
	ASSERT_TRUE(audio.define_arrangement("sequence", simple_arrangement(), error)) << error;

	audio.set_music_paused(true);
	ASSERT_TRUE(audio.play_arrangement("sequence", 0.0f));
	EXPECT_FALSE(audio.arrangement_playback().paused);
	EXPECT_GT(peak(mix_for(audio, 1.5f)), 0.1f);

	audio.set_arrangement_paused(true);
	EXPECT_TRUE(audio.arrangement_playback().paused);
}

TEST(Arrangement, RejectsExcessivePolyphony)
{
	auto arrangement{ simple_arrangement() };
	arrangement.notes.clear();
	for (std::size_t i{}; i <= arrangement_max_voices; ++i)
		arrangement.notes.push_back({ .startBeat = 0.0f, .durationBeats = 2.0f, .instrument = 0,
			.note = 60, .velocity = 1.0f, .pan = 0.0f });

	std::string error{};
	EXPECT_FALSE(prepare_arrangement(std::move(arrangement), 48000, error));
	EXPECT_NE(error.find("polyphony"), std::string::npos);
}

TEST(Arrangement, ReusesVoiceSlotsAtNoteBoundaries)
{
	auto arrangement{ simple_arrangement() };
	arrangement.durationBeats = 2.0f;
	arrangement.instruments.front() = ArrangementInstrument{
		.definition = SynthInstrumentDefinition{
			.wave = InstrumentWave::Sine,
			.envelope = { .attack = 0.0f, .decay = 0.0f, .sustain = 1.0f, .release = 0.0f },
			.gain = 1.0f,
			.lowPassHz = 24000.0f } };
	arrangement.notes.clear();
	for (std::size_t i{}; i < arrangement_max_voices; ++i)
	{
		arrangement.notes.push_back({ .startBeat = 0.0f, .durationBeats = 1.0f, .instrument = 0, .note = 69 });
		arrangement.notes.push_back({ .startBeat = 1.0f, .durationBeats = 1.0f, .instrument = 0, .note = 69 });
	}

	auto audio{ make_offline_audio() };
	std::string error{};
	ASSERT_TRUE(audio.define_arrangement("boundary", std::move(arrangement), error)) << error;
	ASSERT_TRUE(audio.play_arrangement("boundary", 0.0f));
	mix_for(audio, 1.1f);
	EXPECT_GT(peak(mix_for(audio, 0.1f)), 0.1f);
}

TEST(Arrangement, SamplerSeekRestoresSampleOffset)
{
	AudioSampleData sample{};
	sample.sampleRate = 48000;
	sample.channels = 1;
	sample.samples.resize(48000 * 4);
	for (std::size_t i{}; i < sample.samples.size(); ++i)
		sample.samples[i] = std::sin(2.0f * std::numbers::pi_v<float> * 440.0f
			* static_cast<float>(i) / 48000.0f);

	SamplerInstrumentDefinition sampler{};
	sampler.endFrame = sample.frame_count();
	sampler.envelope = { .attack = 0.0f, .decay = 0.0f, .sustain = 1.0f, .release = 0.01f };
	Arrangement arrangement{
		.tempo = 60.0f,
		.durationBeats = 4.0f,
		.instruments = { ArrangementInstrument{ .definition = sampler, .sample = &sample } },
		.notes = { ArrangementNote{ .startBeat = 0.0f, .durationBeats = 4.0f, .instrument = 0,
			.note = 60, .velocity = 1.0f, .pan = 0.0f } } };

	auto uninterrupted{ make_offline_audio() };
	auto sought{ make_offline_audio() };
	std::string error{};
	ASSERT_TRUE(uninterrupted.define_arrangement("sample", arrangement, error)) << error;
	ASSERT_TRUE(sought.define_arrangement("sample", std::move(arrangement), error)) << error;
	ASSERT_TRUE(uninterrupted.play_arrangement("sample", 0.0f));
	ASSERT_TRUE(sought.play_arrangement("sample", 0.0f));

	mix_for(uninterrupted, 1.0f);
	ASSERT_TRUE(sought.seek_arrangement(1.0f));
	const auto uninterruptedAudio{ mix_for(uninterrupted, 0.1f) };
	const auto soughtAudio{ mix_for(sought, 0.1f) };
	ASSERT_EQ(uninterruptedAudio.size(), soughtAudio.size());
	for (std::size_t i{}; i < uninterruptedAudio.size(); ++i)
		EXPECT_NEAR(uninterruptedAudio[i], soughtAudio[i], 1e-5f);
}

TEST(Arrangement, ReverbAddsTailAndSeekClearsIt)
{
	auto arrangement{ simple_arrangement() };
	arrangement.notes = {
		ArrangementNote{ .startBeat = 0.0f, .durationBeats = 0.25f, .instrument = 0,
			.note = 69, .velocity = 1.0f, .pan = 0.0f },
		ArrangementNote{ .startBeat = 3.0f, .durationBeats = 0.25f, .instrument = 0,
			.note = 69, .velocity = 1.0f, .pan = 0.0f },
	};
	arrangement.reverb = { .wet = 0.6f, .decaySeconds = 1.0f, .damping = 0.3f };
	auto dryArrangement{ arrangement };
	dryArrangement.reverb.wet = 0.0f;

	auto audio{ make_offline_audio() };
	auto dryAudio{ make_offline_audio() };
	std::string error{};
	ASSERT_TRUE(audio.define_arrangement("reverb", std::move(arrangement), error)) << error;
	ASSERT_TRUE(dryAudio.define_arrangement("dry", std::move(dryArrangement), error)) << error;
	ASSERT_TRUE(audio.play_arrangement("reverb", 0.0f));
	ASSERT_TRUE(dryAudio.play_arrangement("dry", 0.0f));

	mix_for(audio, 0.5f);
	const auto tail{ mix_for(audio, 0.05f) };
	EXPECT_GT(peak(tail), 1e-5f);

	ASSERT_TRUE(audio.seek_arrangement(2.0f));
	const auto clearedTail{ mix_for(audio, 0.05f) };
	EXPECT_LT(peak(clearedTail), 1e-6f);

	ASSERT_TRUE(audio.seek_arrangement(3.0f));
	ASSERT_TRUE(dryAudio.seek_arrangement(3.0f));
	const auto wetDirect{ mix_for(audio, 0.01f) };
	const auto dryDirect{ mix_for(dryAudio, 0.01f) };
	ASSERT_EQ(wetDirect.size(), dryDirect.size());
	for (std::size_t i{}; i < wetDirect.size(); ++i)
		EXPECT_NEAR(wetDirect[i], dryDirect[i], 1e-5f);

	const auto laterNote{ mix_for(audio, 0.05f) };
	EXPECT_GT(peak(laterNote), 0.1f);
}

TEST(Arrangement, RejectsInvalidReverbSettings)
{
	auto arrangement{ simple_arrangement() };
	arrangement.reverb.wet = 1.01f;

	std::string error{};
	EXPECT_FALSE(prepare_arrangement(std::move(arrangement), 48000, error));
	EXPECT_NE(error.find("reverb"), std::string::npos);
}

TEST(Arrangement, ReverbHandlesMaximumPolyphony)
{
	auto arrangement{ simple_arrangement() };
	arrangement.durationBeats = 2.0f;
	arrangement.notes.clear();
	for (std::size_t i{}; i < arrangement_max_voices; ++i)
	{
		arrangement.notes.push_back({ .startBeat = 0.0f, .durationBeats = 1.0f, .instrument = 0,
			.note = static_cast<std::uint8_t>(48 + i % 24), .velocity = 0.3f, .pan = 0.0f });
	}
	arrangement.reverb = { .wet = 0.4f, .decaySeconds = 1.5f, .damping = 0.4f };

	auto audio{ make_offline_audio() };
	std::string error{};
	ASSERT_TRUE(audio.define_arrangement("dense_reverb", std::move(arrangement), error)) << error;
	ASSERT_TRUE(audio.play_arrangement("dense_reverb", 0.0f));
	const auto output{ mix_for(audio, 1.1f) };
	EXPECT_TRUE(std::ranges::all_of(output, [](float sample) { return std::isfinite(sample); }));
	EXPECT_LE(peak(output), 1.0f);
	EXPECT_GT(peak(output), 0.1f);
}

TEST(Arrangement, ReverbContinuesAcrossArrangementCrossfade)
{
	auto first{ simple_arrangement() };
	first.notes.front().startBeat = 0.0f;
	first.notes.front().durationBeats = 4.0f;
	first.reverb = { .wet = 0.3f, .decaySeconds = 1.0f, .damping = 0.4f };
	auto second{ first };
	second.notes.front().note = 72;

	auto audio{ make_offline_audio() };
	std::string error{};
	ASSERT_TRUE(audio.define_arrangement("first", std::move(first), error)) << error;
	ASSERT_TRUE(audio.define_arrangement("second", std::move(second), error)) << error;
	ASSERT_TRUE(audio.play_arrangement("first", 0.0f));
	mix_for(audio, 0.05f);
	ASSERT_TRUE(audio.play_arrangement("second", 0.05f));

	const auto output{ mix_for(audio, 0.2f) };
	const auto state{ audio.arrangement_playback() };
	EXPECT_EQ(state.name, "second");
	EXPECT_TRUE(state.active);
	EXPECT_TRUE(std::ranges::all_of(output, [](float sample) { return std::isfinite(sample); }));
	EXPECT_GT(peak(output), 0.01f);
}

TEST(InstrumentVoice, SamplerAppliesIndependentSettingsAndPitch)
{
	AudioSampleData sample{};
	sample.sampleRate = 8000;
	sample.channels = 1;
	sample.samples.assign(100, 1.0f);

	SamplerInstrumentDefinition quiet{};
	quiet.rootNote = 60;
	quiet.startFrame = 0;
	quiet.endFrame = sample.frame_count();
	quiet.envelope = { .attack = 0.0f, .decay = 0.0f, .sustain = 1.0f, .release = 0.01f };
	quiet.gain = 0.2f;
	auto loud{ quiet };
	loud.gain = 0.8f;

	SamplerInstrumentVoice quietVoice{ sample, quiet, 72, 1.0f, 8000 };
	SamplerInstrumentVoice loudVoice{ sample, loud, 72, 1.0f, 8000 };
	ASSERT_TRUE(quietVoice.valid());
	ASSERT_TRUE(loudVoice.valid());
	EXPECT_FLOAT_EQ(quiet.gain, 0.2f);
	EXPECT_FLOAT_EQ(loud.gain, 0.8f);

	float quietLeft{}, quietRight{}, loudLeft{}, loudRight{};
	std::size_t frames{};
	while (quietVoice.next(quietLeft, quietRight))
	{
		ASSERT_TRUE(loudVoice.next(loudLeft, loudRight));
		EXPECT_NEAR(loudLeft, quietLeft * 4.0f, 1e-5f);
		++frames;
	}
	EXPECT_EQ(frames, 50u);
}

TEST(InstrumentVoice, SamplerPreservesStereoChannelsAndRejectsBadLoop)
{
	AudioSampleData sample{};
	sample.sampleRate = 8000;
	sample.channels = 2;
	sample.samples = { 0.25f, 0.75f, 0.5f, 1.0f };
	SamplerInstrumentDefinition definition{};
	definition.startFrame = 0;
	definition.endFrame = 2;
	definition.envelope = { .attack = 0.0f, .decay = 0.0f, .sustain = 1.0f, .release = 0.01f };
	SamplerInstrumentVoice voice{ sample, definition, 60, 1.0f, 8000 };
	ASSERT_TRUE(voice.valid());

	float left{}, right{};
	ASSERT_TRUE(voice.next(left, right));
	EXPECT_NEAR(left, 0.25f, 1e-5f);
	EXPECT_NEAR(right, 0.75f, 1e-5f);

	definition.loop = true;
	definition.loopStartFrame = 1;
	definition.loopEndFrame = 1;
	const SamplerInstrumentVoice invalidLoop{ sample, definition, 60, 1.0f, 8000 };
	EXPECT_FALSE(invalidLoop.valid());
}

TEST(AudioSystem, PlaysPitchedSynthAndSamplerVoices)
{
	auto audio{ make_offline_audio() };
	SynthInstrumentDefinition synth{};
	synth.gain = 1.0f;
	synth.envelope = { .attack = 0.0f, .decay = 0.0f, .sustain = 1.0f, .release = 0.01f };

	const auto synthId{ audio.play_synth(synth, 69) };
	ASSERT_NE(synthId, 0u);
	EXPECT_GT(peak(mix_for(audio, 0.01f)), 0.1f);
	EXPECT_TRUE(audio.note_off(synthId));
	mix_for(audio, 0.02f);
	EXPECT_FALSE(audio.is_playing(synthId));
	EXPECT_EQ(audio.play_synth(synth, 69, 1.0f, InstrumentPlayOptions{ .bus = Bus::Master }), 0u);

	AudioSampleData sample{};
	sample.sampleRate = 8000;
	sample.channels = 1;
	sample.samples.assign(1000, 0.5f);
	SamplerInstrumentDefinition sampler{};
	sampler.endFrame = sample.frame_count();
	sampler.envelope = { .attack = 0.0f, .decay = 0.0f, .sustain = 1.0f, .release = 0.01f };

	const auto samplerId{ audio.play_sampler(sample, sampler, 60) };
	ASSERT_NE(samplerId, 0u);
	EXPECT_GT(peak(mix_for(audio, 0.01f)), 0.1f);
	EXPECT_TRUE(audio.note_off(samplerId));
	mix_for(audio, 0.02f);
	EXPECT_FALSE(audio.is_playing(samplerId));
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
	app.set_audio_config(AudioSystem::Config{ .openDevice = false });
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

	ASSERT_TRUE(audio->play_playlist({ a, b },
		{ .shuffle = false, .repeat = true, .fade = 0.0f, .gap = 0.0f }));
	EXPECT_EQ(audio->current_music(), a->asset_name());
	ASSERT_TRUE(audio->previous_track(0.0f));
	EXPECT_EQ(audio->current_music(), b->asset_name());
	ASSERT_TRUE(audio->next_track(0.0f));
	EXPECT_EQ(audio->current_music(), a->asset_name());
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
	// Opens the real output device but plays nothing.
	auto config{ AudioSystem::default_config() };
	if (!config.openDevice)
		GTEST_SKIP() << "Audio output device is disabled";
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
	app.set_audio_config(AudioSystem::Config{ .openDevice = false });
	ASSERT_TRUE(app.try_load({}, wgpu::BackendType::Undefined));
}
