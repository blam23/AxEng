#include <gtest/gtest.h>

#include "axeng/core/audio/audio_system.h"
#include "axeng/core/audio/voice_synth.h"

#include <algorithm>
#include <cmath>
#include <memory>
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
