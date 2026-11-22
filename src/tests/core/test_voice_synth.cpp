#include <gtest/gtest.h>

#include "axeng/core/audio/audio_system.h"
#include "axeng/core/audio/voice_synth.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <span>
#include <vector>

namespace
{
	using namespace ax::audio;

	constexpr std::uint32_t s_rate{ 44100 };

	VoiceSegment vowel(float seconds, float f0Start, float f0End, float amp = 1.0f)
	{
		VoiceSegment segment{};
		segment.kind = VoiceSegmentKind::Voiced;
		segment.seconds = seconds;
		segment.pitchStart = f0Start;
		segment.pitchEnd = f0End;
		segment.ampStart = amp;
		segment.ampEnd = amp;
		segment.formants = { VoiceFormant{ 730.0f, 90.0f }, VoiceFormant{ 1090.0f, 110.0f }, VoiceFormant{ 2440.0f, 160.0f } };
		return segment;
	}

	VoiceSegment gap(float seconds)
	{
		VoiceSegment segment{};
		segment.kind = VoiceSegmentKind::Gap;
		segment.seconds = seconds;
		return segment;
	}

	float rms(std::span<const float> samples)
	{
		double sum{};
		for (const auto s : samples) sum += static_cast<double>(s) * s;
		return samples.empty() ? 0.0f : static_cast<float>(std::sqrt(sum / static_cast<double>(samples.size())));
	}

	// The pitch of a stretch, from the spacing of its strongest autocorrelation peak between 60 and 500 Hz.
	float pitch_of(std::span<const float> samples)
	{
		const auto minLag{ static_cast<std::size_t>(s_rate / 500) };
		const auto maxLag{ static_cast<std::size_t>(s_rate / 60) };
		std::size_t best{ minLag };
		double bestScore{ -1.0 };
		for (auto lag{ minLag }; lag <= maxLag && lag < samples.size(); ++lag)
		{
			double score{};
			for (std::size_t i{}; i + lag < samples.size(); ++i) score += static_cast<double>(samples[i]) * samples[i + lag];
			if (score > bestScore)
			{
				bestScore = score;
				best = lag;
			}
		}
		return static_cast<float>(s_rate) / static_cast<float>(best);
	}

	std::span<const float> window(const RenderedVoice& voice, float from, float to)
	{
		const auto a{ static_cast<std::size_t>(from * s_rate) };
		const auto b{ std::min(voice.samples.size(), static_cast<std::size_t>(to * s_rate)) };
		return std::span<const float>{ voice.samples }.subspan(a, b - a);
	}
}

TEST(VoiceSynth, SameInputGivesIdenticalSamples)
{
	const std::vector segments{ vowel(0.2f, 160.0f, 150.0f), gap(0.05f), vowel(0.15f, 170.0f, 180.0f) };
	const auto first{ render_voice({}, segments, s_rate, 42) };
	const auto second{ render_voice({}, segments, s_rate, 42) };
	EXPECT_EQ(first.samples, second.samples);
	const auto other{ render_voice({}, segments, s_rate, 43) };
	EXPECT_NE(first.samples, other.samples) << "the seed changes the jitter and breath";
}

TEST(VoiceSynth, SegmentsStartWhereTheyAreReported)
{
	const std::vector segments{ vowel(0.1f, 160.0f, 160.0f), gap(0.0731f), vowel(0.2f, 160.0f, 160.0f) };
	const auto voice{ render_voice({}, segments, s_rate, 1) };
	ASSERT_EQ(voice.starts.size(), 4u);
	EXPECT_FLOAT_EQ(voice.starts[0], 0.0f);
	EXPECT_NEAR(voice.starts[1], 0.1f, 1.0f / s_rate);
	EXPECT_NEAR(voice.starts[2], 0.1731f, 1.0f / s_rate);
	EXPECT_NEAR(voice.starts[3], 0.3731f, 1.0f / s_rate);
	EXPECT_NEAR(voice.seconds, 0.3731f, 1.0f / s_rate);
	EXPECT_GT(voice.samples.size(), static_cast<std::size_t>(voice.seconds * s_rate)) << "a tail rings out after";
}

TEST(VoiceSynth, PitchFollowsTheSegments)
{
	const std::vector segments{ vowel(0.4f, 140.0f, 140.0f), vowel(0.4f, 220.0f, 220.0f) };
	VoiceTimbre timbre{};
	timbre.jitter = 0.0f;
	timbre.breath = 0.0f;
	const auto voice{ render_voice(timbre, segments, s_rate, 3) };
	EXPECT_NEAR(pitch_of(window(voice, 0.15f, 0.35f)), 140.0f, 6.0f);
	EXPECT_NEAR(pitch_of(window(voice, 0.55f, 0.75f)), 220.0f, 9.0f);
}

TEST(VoiceSynth, ARisingSegmentRises)
{
	const std::vector segments{ vowel(0.8f, 150.0f, 240.0f) };
	VoiceTimbre timbre{};
	timbre.jitter = 0.0f;
	const auto voice{ render_voice(timbre, segments, s_rate, 4) };
	EXPECT_LT(pitch_of(window(voice, 0.05f, 0.2f)) + 30.0f, pitch_of(window(voice, 0.6f, 0.75f)));
}

TEST(VoiceSynth, GapsAreQuietButTheBreathTailDecays)
{
	const std::vector segments{ vowel(0.2f, 160.0f, 160.0f), gap(0.3f), vowel(0.2f, 160.0f, 160.0f) };
	const auto voice{ render_voice({}, segments, s_rate, 5) };
	const auto loud{ rms(window(voice, 0.05f, 0.18f)) };
	const auto justAfter{ rms(window(voice, 0.2f, 0.215f)) };
	const auto deepInGap{ rms(window(voice, 0.35f, 0.48f)) };
	EXPECT_GT(loud, 0.02f);
	EXPECT_GT(justAfter, deepInGap) << "it decays, not cuts off";
	EXPECT_LT(deepInGap, loud * 0.02f);
}

TEST(VoiceSynth, LouderSegmentsAreLouderAndNothingClips)
{
	const std::vector segments{ vowel(0.3f, 160.0f, 160.0f, 0.5f), vowel(0.3f, 190.0f, 190.0f, 3.0f) };
	const auto voice{ render_voice({}, segments, s_rate, 6) };
	EXPECT_GT(rms(window(voice, 0.4f, 0.55f)), rms(window(voice, 0.1f, 0.25f)) * 1.5f);
	for (const auto s : voice.samples) ASSERT_LE(std::abs(s), 1.0f);
	EXPECT_LE(voice.peak, 1.0f);
}

TEST(VoiceSynth, NoiseSegmentsHiss)
{
	VoiceSegment hiss{};
	hiss.kind = VoiceSegmentKind::Noise;
	hiss.seconds = 0.2f;
	hiss.ampStart = hiss.ampEnd = 0.5f;
	hiss.noiseHz = 5000.0f;
	const auto voice{ render_voice({}, std::vector{ hiss }, s_rate, 7) };
	EXPECT_GT(rms(window(voice, 0.05f, 0.15f)), 0.005f);
}

TEST(VoiceSynth, AnEmptyListRendersOnlySilence)
{
	const auto voice{ render_voice({}, std::span<const VoiceSegment>{}, s_rate, 8) };
	EXPECT_EQ(voice.seconds, 0.0f);
	for (const auto s : voice.samples) EXPECT_EQ(s, 0.0f);
}

TEST(VoiceSynth, PlayBufferPlaysOnceAndFrees)
{
	AudioSystem::Config config{};
	config.openDevice = false;
	config.manualMix = true;
	config.sampleRate = sfx_sample_rate;
	AudioSystem audio{ config };
	auto rendered{ render_voice({}, std::vector{ vowel(0.1f, 160.0f, 160.0f) }, sfx_sample_rate, 9) };
	auto samples{ std::make_shared<const std::vector<float>>(std::move(rendered.samples)) };
	std::weak_ptr<const std::vector<float>> watch{ samples };
	const auto id{ audio.play_buffer(samples, Bus::Ui) };
	ASSERT_NE(id, 0u);
	EXPECT_EQ(audio.play_buffer(nullptr, Bus::Ui), 0u);
	EXPECT_EQ(audio.play_buffer(samples, Bus::Master), 0u) << "never straight to master";
	samples.reset();
	EXPECT_FALSE(watch.expired()) << "the voice keeps its buffer";
	std::vector<float> out(sfx_sample_rate * 2, 0.0f);
	audio.mix(out.data(), sfx_sample_rate);
	EXPECT_GT(*std::ranges::max_element(out), 0.0f);
	audio.mix(out.data(), sfx_sample_rate);
	EXPECT_FALSE(audio.is_playing(id));
	audio.mix(out.data(), 256);
	EXPECT_TRUE(watch.expired()) << "and lets it go once done";
}


TEST(VoiceSynth, PopsSoundAndTheExtraLayersNeverClip)
{
	VoiceSegment pop{};
	pop.kind = VoiceSegmentKind::Pop;
	pop.seconds = 0.03f;
	pop.pitchStart = 900.0f;
	pop.pitchEnd = 300.0f;
	pop.ampStart = pop.ampEnd = 1.0f;
	const auto popped{ render_voice({}, std::vector{ gap(0.02f), pop, gap(0.05f) }, s_rate, 10) };
	EXPECT_GT(rms(window(popped, 0.02f, 0.05f)), 0.02f) << "a pop is heard";
	EXPECT_LT(rms(window(popped, 0.07f, 0.1f)), 0.005f) << "and gone quickly";

	VoiceTimbre alien{};
	alien.wobbleDepth = 0.2f;
	alien.wobblePitch = 0.1f;
	alien.subharmonic = 1.0f;
	alien.ringDepth = 1.0f;
	const std::vector segments{ vowel(0.3f, 160.0f, 150.0f, 2.0f), pop, vowel(0.3f, 180.0f, 170.0f, 2.0f) };
	const auto voice{ render_voice(alien, segments, s_rate, 11) };
	for (const auto s : voice.samples) ASSERT_TRUE(std::isfinite(s) && std::abs(s) <= 1.0f);
	const auto plain{ render_voice({}, segments, s_rate, 11) };
	EXPECT_NE(voice.samples, plain.samples) << "the layers change the sound";
}

TEST(VoiceSynth, AUnisonChoirSoundsAndSmoothnessSoftens)
{
	const std::vector segments{ vowel(0.5f, 160.0f, 160.0f) };
	VoiceTimbre choir{};
	choir.unison = 5;
	choir.detuneCents = 15.0f;
	choir.smooth = 0.8f;
	const auto sung{ render_voice(choir, segments, s_rate, 12) };
	const auto plain{ render_voice({}, segments, s_rate, 12) };
	for (const auto s : sung.samples) ASSERT_TRUE(std::isfinite(s) && std::abs(s) <= 1.0f);
	EXPECT_NE(sung.samples, plain.samples);
	EXPECT_GT(rms(window(sung, 0.1f, 0.4f)), 0.01f) << "the choir is heard";
	// Smoothness softens the source.
	VoiceTimbre soft{};
	soft.smooth = 1.0f;
	// Brightness as the share of energy above about 2 kHz: a one-pole high pass over the samples.
	const auto bright = [](const RenderedVoice& voice)
	{
		double high{}, all{}, y{}, previous{};
		const auto a{ std::exp(-2.0 * std::numbers::pi * 2000.0 / s_rate) };
		for (const auto s : voice.samples)
		{
			y = a * (y + s - previous);
			previous = s;
			high += y * y;
			all += static_cast<double>(s) * s;
		}
		return all > 0.0 ? high / all : 0.0;
	};
	EXPECT_LT(bright(render_voice(soft, segments, s_rate, 12)), bright(plain) * 0.8) << "smooth is less buzzy";
	EXPECT_LT(bright(sung), bright(plain)) << "and so is the smooth choir";
}

TEST(VoiceSynth, AReverseReverbSwellsIntoEachSound)
{
	const std::vector segments{ gap(0.3f), vowel(0.3f, 160.0f, 160.0f) };
	VoiceTimbre swell{};
	swell.reverseMix = 0.5f;
	swell.reverseLead = 0.25f;
	const auto dry{ render_voice({}, segments, s_rate, 13) };
	const auto wet{ render_voice(swell, segments, s_rate, 13) };
	EXPECT_NEAR(wet.lead, 0.25f, 1.0f / s_rate);
	EXPECT_EQ(wet.starts, dry.starts) << "the segments keep their times, counted from after the lead-in";
	// Before the vowel (in the gap) the dry voice is silent; the reversed tail swells there, rising toward it.
	const auto quiet{ rms(window(dry, 0.1f, 0.28f)) };
	const auto early{ rms(window(wet, 0.25f + 0.02f, 0.25f + 0.12f)) };
	const auto late{ rms(window(wet, 0.25f + 0.2f, 0.25f + 0.29f)) };
	EXPECT_LT(quiet, 0.001f);
	EXPECT_GT(late, 0.002f) << "a swell before the sound";
	EXPECT_GT(late, early) << "rising into it";
	for (const auto s : wet.samples) ASSERT_TRUE(std::isfinite(s) && std::abs(s) <= 1.0f);
}