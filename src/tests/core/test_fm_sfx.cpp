#include <gtest/gtest.h>

#include "axeng/core/application.h"
#include "axeng/core/audio/audio_system.h"
#include "axeng/core/audio/fm_sfx.h"
#include "tests/main/log_capture.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <vector>

namespace
{
	using namespace ax::audio;

	float peak_of(std::span<const float> samples)
	{
		float result{};
		for (const auto s : samples)
			result = std::max(result, std::abs(s));
		return result;
	}

	float rms_of(std::span<const float> samples)
	{
		double sum{};
		for (const auto s : samples)
			sum += static_cast<double>(s) * s;
		return samples.empty() ? 0.0f : static_cast<float>(std::sqrt(sum / static_cast<double>(samples.size())));
	}

	// A held, feedback-free tone with beating carriers and a slow amplitude LFO: everything periodic.
	FmSfxParams periodic_hum(std::uint8_t feedback = 0)
	{
		FmSfxParams params{};
		params.note = 40;
		params.hold = 1.0f;
		auto& patch{ params.patch };
		patch.algorithm = 5;
		patch.feedback = feedback;
		patch.gain = 0.4f;
		patch.envelope = { .attack = 0.0f, .decay = 0.0f, .sustain = 1.0f, .release = 0.2f };
		for (std::size_t i{}; i < fm_operator_count; ++i)
		{
			auto& op{ patch.operators[i] };
			op.envelope = { .rates = { 80, 99, 99, 60 }, .levels = { 99, 99, 99, 0 } };
			op.outputLevel = i % 2 == 0 ? 95 : 45;
			op.coarse = static_cast<std::uint8_t>(i / 2 + 1);
			op.fine = static_cast<std::uint8_t>(i * 7);
			op.detune = static_cast<std::int8_t>(static_cast<int>(i) - 3);
			op.ampModSensitivity = 2;
		}
		patch.lfo = { .wave = FmLfoWave::Sine, .speed = 23, .ampModDepth = 50 };
		return params;
	}

	void run_lua(const std::string& name, const std::string& entry_point)
	{
		const std::string manifest{ "manifest = { name = '" + name + "', headless = true, audio = true, permissions = {}, "
			"scripts = { main = 'main.lua' }, entry_point = 'main' }" };
		ax::EmbeddedResourceLayout layout;
		layout.emplace("manifest.luac", ax::EmbeddedResource{ reinterpret_cast<const std::uint8_t*>(manifest.data()), manifest.size() });
		layout.emplace("main.lua", ax::EmbeddedResource{ reinterpret_cast<const std::uint8_t*>(entry_point.data()), entry_point.size() });
		auto app{ ax::Application::from_embedded({}, std::move(layout)) };
		app.set_audio_config(AudioSystem::Config{ .openDevice = false });
		ASSERT_TRUE(app.try_load({}, wgpu::BackendType::Undefined));
	}
}

TEST(FmSfx, EveryPresetIsBoundedAndAudible)
{
	EXPECT_GE(fm_preset_names().size(), 10u);
	for (const auto name : fm_preset_names())
	{
		for (const std::uint64_t seed : { 1u, 2u, 3u })
		{
			const auto params{ generate_fm_preset(name, seed) };
			ASSERT_TRUE(params) << name;
			for (const bool loop : { false, true })
			{
				const auto samples{ render_fm_sfx(*params, { .loop = loop }) };
				ASSERT_FALSE(samples.empty()) << name << " seed " << seed << " loop " << loop;
				EXPECT_LE(samples.size(), static_cast<std::size_t>(sfx_max_length * sfx_sample_rate)) << name;
				const auto peak{ peak_of(samples) };
				EXPECT_LE(peak, 1.0f) << name;
				EXPECT_LT(peak, 0.98f) << name << " clips (seed " << seed << ", loop " << loop << ")";
				if (!loop)
					EXPECT_GT(rms_of(samples), 0.01f) << name << " seed " << seed;
			}
		}
	}
	EXPECT_FALSE(generate_fm_preset("nope", 1));
}

TEST(FmSfx, RenderingIsDeterministic)
{
	const auto a{ generate_fm_preset("chime", 5) };
	const auto b{ generate_fm_preset("chime", 5) };
	const auto c{ generate_fm_preset("chime", 6) };
	ASSERT_TRUE(a && b && c);
	EXPECT_EQ(render_fm_sfx(*a), render_fm_sfx(*b));
	EXPECT_EQ(render_fm_sfx(*a, { .loop = true }), render_fm_sfx(*b, { .loop = true }));
	EXPECT_NE(render_fm_sfx(*a), render_fm_sfx(*c));
}

TEST(FmSfx, HoldAndReleaseSetTheLengthUpToTheCap)
{
	auto params{ periodic_hum() };
	params.hold = 0.5f;
	params.patch.envelope.release = 0.25f;
	const auto samples{ render_fm_sfx(params) };
	EXPECT_NEAR(static_cast<float>(samples.size()) / sfx_sample_rate, 0.75f, 0.01f);

	params.hold = 30.0f;
	EXPECT_EQ(render_fm_sfx(params).size(), static_cast<std::size_t>(sfx_max_length * sfx_sample_rate));

	params.patch.algorithm = 0;
	EXPECT_TRUE(render_fm_sfx(params).empty());
}

TEST(FmSfx, MutateKeepsTheVoiceAndStaysInRange)
{
	const auto base{ *generate_fm_preset("metal_hit", 4) };
	const auto same{ mutate_fm(base, 0.0f, 9) };
	EXPECT_EQ(render_fm_sfx(same), render_fm_sfx(base));

	bool changed{};
	for (std::uint64_t seed{ 1 }; seed <= 20; ++seed)
	{
		const auto mutated{ mutate_fm(base, 0.3f, seed) };
		EXPECT_EQ(mutated.patch.algorithm, base.patch.algorithm);
		EXPECT_LE(mutated.patch.feedback, 7);
		EXPECT_EQ(mutated.note, base.note);
		for (std::size_t i{}; i < fm_operator_count; ++i)
		{
			const auto& op{ mutated.patch.operators[i] };
			const auto& original{ base.patch.operators[i] };
			EXPECT_EQ(op.coarse, original.coarse);
			EXPECT_EQ(op.fixedFrequency, original.fixedFrequency);
			EXPECT_EQ(op.outputLevel == 0, original.outputLevel == 0);
			EXPECT_LE(op.outputLevel, 99);
			EXPECT_LE(op.fine, 99);
			EXPECT_GE(op.detune, -7);
			EXPECT_LE(op.detune, 7);
			for (const auto rate : op.envelope.rates)
				EXPECT_LE(rate, 99);
			changed = changed || op.outputLevel != original.outputLevel || op.envelope.rates != original.envelope.rates;
		}
	}
	EXPECT_TRUE(changed);
}

TEST(FmSfx, LoopLengthsArePowersOfTwo)
{
	EXPECT_EQ(fm_sfx_loop_frames(1.5f), 1u << 16);
	EXPECT_EQ(fm_sfx_loop_frames(3.0f), 1u << 17);
	EXPECT_EQ(fm_sfx_loop_frames(6.0f), 1u << 18);
	EXPECT_EQ(fm_sfx_loop_frames(0.1f), 1u << 16);
	EXPECT_EQ(fm_sfx_loop_frames(100.0f), 1u << 18);
	EXPECT_EQ(fm_sfx_loop_frames(std::nanf("")), 1u << 16);
}

TEST(FmSfx, SnappedLoopsRepeatSampleForSample)
{
	const auto params{ periodic_hum() };
	constexpr std::uint32_t loop{ 1u << 16 };
	FmInstrumentVoice voice{ params.patch, params.note, params.velocity, sfx_sample_rate };
	ASSERT_TRUE(voice.valid());
	voice.snap_to_loop(loop);
	float sample{};
	for (std::uint32_t i{}; i < sfx_sample_rate * 4 && !voice.settled(); ++i)
		voice.next(sample);
	ASSERT_TRUE(voice.settled());
	for (std::uint32_t i{}; i < sfx_sample_rate; ++i)
		voice.next(sample);

	std::vector<float> rendered(loop * 2);
	for (auto& value : rendered)
		voice.next(value);
	ASSERT_GT(rms_of(rendered), 0.02f);
	float worst{};
	for (std::uint32_t i{}; i < loop; ++i)
		worst = std::max(worst, std::abs(rendered[i] - rendered[i + loop]));
	EXPECT_LT(worst, 1e-4f);

	// The rendered loop keeps the nominal length because the splice search finds nothing better.
	auto sfx{ params };
	sfx.loopLength = 1.5f;
	EXPECT_EQ(render_fm_sfx(sfx, { .loop = true }).size(), loop);
}

TEST(FmSfx, LoopSeamsLookLikeAnyOtherStep)
{
	// Feedback-free loops repeat exactly; strong feedback relies on the splice search and crossfade.
	for (const std::uint8_t feedback : { std::uint8_t{ 0 }, std::uint8_t{ 7 } })
	{
		const auto samples{ render_fm_sfx(periodic_hum(feedback), { .loop = true }) };
		ASSERT_FALSE(samples.empty());
		EXPECT_LE(loop_seam(samples), 1.01f) << "feedback " << static_cast<int>(feedback);
	}
	for (const auto name : fm_preset_names())
	{
		for (const std::uint64_t seed : { 1u, 2u, 3u })
		{
			const auto samples{ render_fm_sfx(*generate_fm_preset(name, seed), { .loop = true }) };
			EXPECT_LE(loop_seam(samples), 1.01f) << name << " seed " << seed;
		}
	}
}

TEST(SfxSynth, EveryLoopedPresetHasASeamlessWrap)
{
	for (const auto name : preset_names())
	{
		for (const std::uint64_t seed : { 1u, 7u })
		{
			const auto samples{ synthesize(*generate_preset(name, seed), { .loop = true, .seed = seed }) };
			ASSERT_FALSE(samples.empty()) << name;
			EXPECT_LE(loop_seam(samples), 1.01f) << name << " seed " << seed;
		}
	}
}

TEST(SfxSynth, SeamlessLoopKeepsAnExactRepeat)
{
	constexpr std::size_t period{ 100 };
	std::vector<float> rendered(period * 50);
	for (std::size_t i{}; i < rendered.size(); ++i)
		rendered[i] = std::sin(static_cast<float>(i % period) / period * 6.2831853f) * 0.5f;
	const auto loop{ make_seamless_loop(rendered, period * 40, 64, 220) };
	ASSERT_EQ(loop.size(), period * 40);
	for (std::size_t i{}; i < loop.size(); ++i)
		ASSERT_NEAR(loop[i], rendered[i], 1e-6f);
	EXPECT_LE(loop_seam(loop), 1.01f);
	EXPECT_FLOAT_EQ(loop_seam(std::vector<float>{ 0.0f, 0.0f }), 0.0f);
}

TEST(AudioSystem, FmSoundsPlayAndReportTheirSeam)
{
	AudioSystem audio{ AudioSystem::Config{ .openDevice = false, .manualMix = true, .sampleRate = 48000, .seed = 1 } };
	SoundDefinition chime{};
	chime.fm = generate_fm_preset("chime", 2);
	chime.variants = 3;
	chime.mutation = 0.05f;
	ASSERT_TRUE(audio.define_sound("chime", chime));
	const auto info{ audio.sound_info("chime") };
	ASSERT_TRUE(info);
	EXPECT_TRUE(info->fm);
	EXPECT_FALSE(info->loop);
	EXPECT_FLOAT_EQ(info->seam, 0.0f);
	EXPECT_GT(info->length, 1.0f);
	EXPECT_GT(info->peak, 0.05f);
	EXPECT_NE(audio.play("chime"), 0u);

	// A live redefinition from an sfxr loop to an FM loop keeps the voice playing on the new sound.
	SoundDefinition hum{};
	hum.params = *generate_preset("hum", 3);
	hum.loop = true;
	ASSERT_TRUE(audio.define_sound("hum", hum));
	const auto id{ audio.play("hum") };
	ASSERT_NE(id, 0u);
	hum.fm = periodic_hum();
	ASSERT_TRUE(audio.define_sound("hum", hum, true));
	EXPECT_TRUE(audio.is_playing(id));
	const auto loopInfo{ audio.sound_info("hum") };
	ASSERT_TRUE(loopInfo);
	EXPECT_TRUE(loopInfo->fm && loopInfo->loop);
	EXPECT_EQ(loopInfo->length, static_cast<float>(1u << 17) / sfx_sample_rate);
	EXPECT_LE(loopInfo->seam, 1.01f);
}

TEST(AudioSystem, LuaDefinesFmSounds)
{
	// Unknown FM preset, the bad algorithm and its sound, the bad note and the unknown wave.
	testlog::LogCapture::instance().expect_total_error_count(5);
	run_lua("axeng_test_fm_sfx", R"(
		local audio = app.audio
		local presets = audio.fm_presets()
		assert(#presets >= 10 and presets[1] == 'chime')
		local generated = audio.generate_fm('alarm', 4)
		assert(generated.fm.type == 'fm' and #generated.fm.operators == 6 and generated.fm.lfo.wave == 'square')
		assert(generated.note >= 0 and generated.hold > 0 and generated.loop_length > 0)
		assert(audio.generate_fm('nope') == nil)
		local mutated = audio.mutate_fm(generated.fm, 0.2, 3)
		assert(mutated.algorithm == generated.fm.algorithm and #mutated.operators == 6)
		local same = audio.mutate_fm(generated.fm, 0, 3)
		assert(same.operators[2].output_level == generated.fm.operators[2].output_level)

		assert(audio.define('from_preset', { fm_preset = 'chime', seed = 2 }))
		assert(audio.define('alarm', { fm = generated.fm, note = 70, hold = 0.3, loop = true, loop_length = 1.5 }))
		local info = audio.info('alarm')
		-- The alarm bends its pitch, so the splice may move the end by up to 5 ms.
		assert(info.fm and info.loop and info.seam <= 1.01 and math.abs(info.length - 65536 / 44100) < 0.006)
		assert(audio.info('from_preset').fm and not audio.info('from_preset').loop)

		assert(not audio.define('bad_preset', { fm_preset = 'nope' }))
		assert(not audio.define('bad_patch', { fm = { algorithm = 40, operators = {} } }))
		assert(not audio.define('bad_note', { fm_preset = 'chime', note = 200 }))
		assert(not audio.define('bad_wave', { params = { wave = 'wobble' } }))
		return 0
	)");
}
