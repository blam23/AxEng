#include <gtest/gtest.h>

#include "axeng/core/audio/fm_voice.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

namespace
{
	using namespace ax::audio;

	constexpr std::uint32_t s_rate{ 48000 };

	// Operator 1 sounding alone in the all-carriers algorithm.
	FmInstrumentDefinition single_carrier()
	{
		FmInstrumentDefinition definition{};
		definition.algorithm = 32;
		definition.gain = 1.0f;
		for (auto& op : definition.operators)
			op.outputLevel = 0;
		definition.operators[0].outputLevel = 99;
		return definition;
	}

	// Operator 2 modulating operator 1, with the other operators silent.
	FmInstrumentDefinition simple_pair(std::uint8_t modulatorLevel)
	{
		auto definition{ single_carrier() };
		definition.algorithm = 1;
		definition.operators[1].outputLevel = modulatorLevel;
		return definition;
	}

	std::vector<float> render(const FmInstrumentDefinition& definition, std::uint8_t note, float velocity,
		std::size_t frames, std::uint32_t rate = s_rate)
	{
		FmInstrumentVoice voice{ definition, note, velocity, rate };
		EXPECT_TRUE(voice.valid());
		std::vector<float> samples(frames);
		for (auto& sample : samples)
			EXPECT_TRUE(voice.next(sample));
		return samples;
	}

	float rms(const std::vector<float>& samples, std::size_t first, std::size_t last)
	{
		double sum{};
		for (auto i{ first }; i < last; ++i)
			sum += static_cast<double>(samples[i]) * samples[i];
		return static_cast<float>(std::sqrt(sum / static_cast<double>(last - first)));
	}

	float peak(const std::vector<float>& samples)
	{
		float result{};
		for (const auto sample : samples)
			result = std::max(result, std::abs(sample));
		return result;
	}

	double crossings_per_second(const std::vector<float>& samples, std::size_t first, std::size_t last, std::uint32_t rate = s_rate)
	{
		std::size_t crossings{};
		for (auto i{ first + 1 }; i < last; ++i)
			crossings += (samples[i - 1] < 0.0f) != (samples[i] < 0.0f);
		return static_cast<double>(crossings) / 2.0 * rate / static_cast<double>(last - first);
	}

	// Amplitude of one frequency, from a single-bin DFT with a Hann window.
	double tone_level(const std::vector<float>& samples, double frequency)
	{
		double real{};
		double imaginary{};
		const auto size{ static_cast<double>(samples.size()) };
		for (std::size_t i{}; i < samples.size(); ++i)
		{
			const auto index{ static_cast<double>(i) };
			const auto window{ 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * index / size) };
			const auto angle{ 2.0 * std::numbers::pi * frequency * index / s_rate };
			real += samples[i] * window * std::cos(angle);
			imaginary += samples[i] * window * std::sin(angle);
		}
		return std::hypot(real, imaginary) * 4.0 / size;
	}
}

TEST(FmAlgorithm, TableIsWellFormed)
{
	EXPECT_EQ(fm_algorithm(0), nullptr);
	EXPECT_EQ(fm_algorithm(33), nullptr);
	for (std::uint8_t number{ 1 }; number <= fm_algorithm_count; ++number)
	{
		const auto* const algorithm{ fm_algorithm(number) };
		ASSERT_NE(algorithm, nullptr);
		EXPECT_NE(algorithm->carriers, 0);
		EXPECT_LT(algorithm->feedbackTarget, fm_operator_count);
		EXPECT_LT(algorithm->feedbackSource, fm_operator_count);
		EXPECT_GE(algorithm->feedbackTarget, algorithm->feedbackSource);

		std::uint32_t modulating{};
		for (std::size_t op{}; op < fm_operator_count; ++op)
		{
			const std::uint32_t modulators{ algorithm->modulators[op] };
			modulating |= modulators;
			EXPECT_EQ(modulators & ((2u << op) - 1), 0u) << "algorithm " << int{ number } << " operator " << op + 1;
		}
		EXPECT_EQ(modulating | algorithm->carriers, 0x3fu) << "algorithm " << int{ number };
		EXPECT_EQ(modulating & algorithm->carriers, 0u) << "algorithm " << int{ number };
	}
}

TEST(FmAlgorithm, CarrierCountsAndFeedbackLoopsMatchTheCharts)
{
	constexpr std::array<int, 32> carriers{ 2, 2, 2, 2, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
		1, 1, 3, 3, 4, 4, 4, 5, 5, 3, 3, 3, 4, 4, 5, 6 };
	for (std::uint8_t number{ 1 }; number <= fm_algorithm_count; ++number)
		EXPECT_EQ(std::popcount(fm_algorithm(number)->carriers), carriers[number - 1u]) << int{ number };

	EXPECT_EQ(fm_algorithm(4)->feedbackTarget, 5);
	EXPECT_EQ(fm_algorithm(4)->feedbackSource, 3);
	EXPECT_EQ(fm_algorithm(6)->feedbackTarget, 5);
	EXPECT_EQ(fm_algorithm(6)->feedbackSource, 4);
	EXPECT_EQ(fm_algorithm(2)->feedbackTarget, 1);
}

TEST(FmVoice, SingleCarrierIsASineAtTheMidiPitch)
{
	const auto samples{ render(single_carrier(), 69, 1.0f, s_rate / 2) };
	EXPECT_NEAR(crossings_per_second(samples, 1000, samples.size()), 440.0, 2.0);
	EXPECT_NEAR(peak(samples), 1.0f, 0.05f);
	EXPECT_LT(tone_level(samples, 880.0), 0.001);
}

TEST(FmVoice, RatiosFixedFrequenciesAndTranspose)
{
	auto definition{ single_carrier() };
	definition.operators[0].coarse = 2;
	auto samples{ render(definition, 69, 1.0f, s_rate / 2) };
	EXPECT_NEAR(crossings_per_second(samples, 1000, samples.size()), 880.0, 3.0);

	definition.operators[0].coarse = 0;
	samples = render(definition, 69, 1.0f, s_rate / 2);
	EXPECT_NEAR(crossings_per_second(samples, 1000, samples.size()), 220.0, 3.0);

	definition.operators[0].coarse = 1;
	definition.operators[0].fine = 50;
	samples = render(definition, 69, 1.0f, s_rate / 2);
	EXPECT_NEAR(crossings_per_second(samples, 1000, samples.size()), 660.0, 3.0);

	definition.operators[0].fine = 0;
	definition.transpose = 12;
	samples = render(definition, 69, 1.0f, s_rate / 2);
	EXPECT_NEAR(crossings_per_second(samples, 1000, samples.size()), 880.0, 3.0);

	definition.transpose = 0;
	definition.operators[0].fixedFrequency = true;
	definition.operators[0].coarse = 2;
	for (const std::uint8_t note : { std::uint8_t{ 40 }, std::uint8_t{ 90 } })
	{
		samples = render(definition, note, 1.0f, s_rate / 2);
		EXPECT_NEAR(crossings_per_second(samples, 1000, samples.size()), 100.0, 3.0);
	}
}

TEST(FmVoice, OutputTrimScalesTheVoice)
{
	auto definition{ single_carrier() };
	definition.gain = 0.25f;
	definition.outputTrim = 2.0f;
	EXPECT_NEAR(peak(render(definition, 69, 1.0f, s_rate / 2)), 0.5f, 0.03f);
}

TEST(FmVoice, DetunedCarriersBeat)
{
	auto definition{ single_carrier() };
	definition.operators[1].outputLevel = 99;
	definition.operators[0].detune = 7;
	definition.operators[1].detune = -7;
	const auto samples{ render(definition, 69, 1.0f, s_rate * 2) };

	float quietest{ 1.0f };
	float loudest{};
	constexpr std::size_t window{ s_rate / 50 };
	for (std::size_t start{}; start + window <= samples.size(); start += window)
	{
		const auto level{ rms(samples, start, start + window) };
		quietest = std::min(quietest, level);
		loudest = std::max(loudest, level);
	}
	EXPECT_LT(quietest, loudest * 0.5f);
}

TEST(FmVoice, ModulatorsAddSidebandsOnlyWhenRoutedToACarrier)
{
	const auto plain{ render(simple_pair(0), 69, 1.0f, s_rate / 2) };
	const auto modulated{ render(simple_pair(85), 69, 1.0f, s_rate / 2) };
	EXPECT_LT(tone_level(plain, 880.0), 0.001);
	EXPECT_GT(tone_level(modulated, 880.0), 0.02);
	EXPECT_GT(tone_level(modulated, 1320.0), tone_level(plain, 1320.0));

	auto silentCarrier{ simple_pair(99) };
	silentCarrier.operators[0].outputLevel = 0;
	EXPECT_LT(peak(render(silentCarrier, 69, 1.0f, 4800)), 0.0001f);

	silentCarrier.algorithm = 32;
	EXPECT_GT(peak(render(silentCarrier, 69, 1.0f, 4800)), 0.9f);
}

TEST(FmVoice, DeepChainsReachTheCarrierThroughEveryOperator)
{
	FmInstrumentDefinition definition{};
	definition.algorithm = 1;
	definition.gain = 1.0f;
	for (auto& op : definition.operators)
		op.outputLevel = 0;
	definition.operators[2].outputLevel = 99;
	const auto clean{ render(definition, 60, 1.0f, s_rate / 2) };

	definition.operators[3].outputLevel = 80;
	definition.operators[4].outputLevel = 80;
	definition.operators[5].outputLevel = 80;
	const auto chained{ render(definition, 60, 1.0f, s_rate / 2) };
	EXPECT_NE(clean, chained);

	definition.operators[3].outputLevel = 0;
	EXPECT_EQ(clean, render(definition, 60, 1.0f, s_rate / 2));
}

TEST(FmVoice, FeedbackBrightensAndStaysStable)
{
	auto saw{ single_carrier() };
	const auto clean{ render(saw, 69, 1.0f, s_rate / 2) };
	saw.feedback = 7;
	saw.operators[0].outputLevel = 0;
	saw.operators[5].outputLevel = 99;
	const auto bright{ render(saw, 69, 1.0f, s_rate / 2) };
	EXPECT_GT(tone_level(bright, 880.0), tone_level(clean, 880.0) + 0.05);
	EXPECT_LE(peak(bright), 1.5f);
	EXPECT_TRUE(std::ranges::all_of(bright, [](float sample) { return std::isfinite(sample); }));
}

TEST(FmVoice, EveryAlgorithmStaysFiniteAtMaximumFeedback)
{
	FmInstrumentDefinition definition{};
	definition.gain = 1.0f;
	definition.feedback = 7;
	for (std::uint8_t number{ 1 }; number <= fm_algorithm_count; ++number)
	{
		definition.algorithm = number;
		const auto samples{ render(definition, 60, 1.0f, 9600) };
		EXPECT_TRUE(std::ranges::all_of(samples, [](float sample) { return std::isfinite(sample); })) << int{ number };
		EXPECT_LE(peak(samples), 1.5f) << int{ number };
		EXPECT_GT(peak(samples), 0.1f) << int{ number };
	}
}

TEST(FmVoice, OperatorEnvelopeDecaysAtTheRequestedRate)
{
	auto fast{ single_carrier() };
	fast.operators[0].envelope = { .rates = { 99, 60, 99, 99 }, .levels = { 99, 0, 0, 0 } };
	auto slow{ fast };
	slow.operators[0].envelope.rates[1] = 40;

	const auto fastSamples{ render(fast, 69, 1.0f, s_rate * 2) };
	const auto slowSamples{ render(slow, 69, 1.0f, s_rate * 2) };
	const auto tail{ [](const std::vector<float>& samples) { return rms(samples, s_rate / 2, s_rate); } };
	EXPECT_GT(rms(fastSamples, 0, 480), 0.5f);
	EXPECT_LT(tail(fastSamples), tail(slowSamples) * 0.1f);
	EXPECT_LT(rms(fastSamples, s_rate + s_rate / 2, s_rate * 2), 0.001f);
}

TEST(FmVoice, DecayIsLinearInDecibels)
{
	auto definition{ single_carrier() };
	definition.operators[0].envelope = { .rates = { 99, 45, 99, 99 }, .levels = { 99, 0, 0, 0 } };
	const auto samples{ render(definition, 69, 1.0f, s_rate * 2) };
	const auto level{ [&](double seconds)
	{
		const auto first{ static_cast<std::size_t>(seconds * s_rate) };
		return 20.0 * std::log10(rms(samples, first, first + 960));
	} };
	EXPECT_NEAR(level(0.2) - level(0.4), level(0.4) - level(0.6), 1.0);
	EXPECT_GT(level(0.2) - level(0.4), 3.0);
}

TEST(FmVoice, SlowAttacksRiseAndHoldTheThirdLevel)
{
	auto definition{ single_carrier() };
	definition.operators[0].envelope = { .rates = { 50, 99, 99, 99 }, .levels = { 99, 80, 80, 0 } };
	const auto samples{ render(definition, 69, 1.0f, s_rate) };
	EXPECT_LT(rms(samples, 0, 240), rms(samples, 2400, 2880));
	const auto held{ rms(samples, s_rate / 2, s_rate) };
	EXPECT_NEAR(held, std::exp2(-19.0f / 8.0f) / std::numbers::sqrt2_v<float>, 0.01f);
}

TEST(FmVoice, RateScalingShortensHighNotes)
{
	auto definition{ single_carrier() };
	definition.operators[0].envelope = { .rates = { 99, 45, 99, 99 }, .levels = { 99, 0, 0, 0 } };
	const auto unscaled{ render(definition, 96, 1.0f, s_rate) };
	definition.operators[0].rateScaling = 7;
	const auto scaled{ render(definition, 96, 1.0f, s_rate) };
	EXPECT_LT(rms(scaled, s_rate / 2, s_rate), rms(unscaled, s_rate / 2, s_rate) * 0.5f);
}

TEST(FmVoice, LevelScalingFollowsTheKeyboard)
{
	auto definition{ single_carrier() };
	auto& op{ definition.operators[0] };
	op.levelScalingBreakpoint = 39;
	op.levelScalingRightDepth = 60;
	op.levelScalingRightCurve = FmScalingCurve::NegativeLinear;
	op.levelScalingLeftDepth = 0;
	const auto level{ [&](std::uint8_t note) { return rms(render(definition, note, 1.0f, 4800), 480, 4800); } };
	EXPECT_NEAR(level(60), level(40), 0.03f);
	EXPECT_LT(level(84), level(60) * 0.5f);

	op.levelScalingRightCurve = FmScalingCurve::NegativeExponential;
	EXPECT_LT(level(100), level(72) * 0.5f);
	EXPECT_GT(level(66), level(60) * 0.9f);
}

TEST(FmVoice, VelocitySensitivityChangesLevelAndBrightness)
{
	auto definition{ single_carrier() };
	const auto level{ [&](float velocity) { return rms(render(definition, 69, velocity, 4800), 480, 4800); } };
	EXPECT_FLOAT_EQ(level(0.2f), level(1.0f));

	definition.operators[0].velocitySensitivity = 4;
	EXPECT_LT(level(0.2f), level(1.0f) * 0.25f);

	auto pair{ simple_pair(70) };
	pair.operators[1].velocitySensitivity = 5;
	const auto soft{ render(pair, 69, 0.2f, s_rate / 2) };
	const auto hard{ render(pair, 69, 1.0f, s_rate / 2) };
	EXPECT_GT(tone_level(hard, 880.0), tone_level(soft, 880.0) * 3.0);
}

TEST(FmVoice, PitchEnvelopeBendsIntoTheNote)
{
	auto definition{ single_carrier() };
	definition.pitchEnvelope = { .rates = { 99, 60, 99, 99 }, .levels = { 74, 50, 50, 74 } };
	const auto samples{ render(definition, 69, 1.0f, s_rate * 2) };
	EXPECT_GT(crossings_per_second(samples, 0, 1200), 500.0);
	EXPECT_NEAR(crossings_per_second(samples, s_rate, s_rate * 2), 440.0, 2.0);
}

TEST(FmVoice, LfoAddsVibratoAndTremoloAfterItsDelay)
{
	auto definition{ single_carrier() };
	definition.lfo = { .wave = FmLfoWave::Sine, .speed = 35, .pitchModDepth = 99, .pitchModSensitivity = 5 };
	auto samples{ render(definition, 69, 1.0f, s_rate) };

	double lowest{ 1.0e9 };
	double highest{};
	for (std::size_t start{}; start + 2400 <= samples.size(); start += 2400)
	{
		const auto rate{ crossings_per_second(samples, start, start + 2400) };
		lowest = std::min(lowest, rate);
		highest = std::max(highest, rate);
	}
	EXPECT_GT(highest - lowest, 60.0);

	definition.lfo = { .wave = FmLfoWave::Square, .speed = 35, .ampModDepth = 99 };
	definition.operators[0].ampModSensitivity = 3;
	samples = render(definition, 69, 1.0f, s_rate);
	float quietest{ 1.0f };
	float loudest{};
	for (std::size_t start{}; start + 1200 <= samples.size(); start += 1200)
	{
		const auto level{ rms(samples, start, start + 1200) };
		quietest = std::min(quietest, level);
		loudest = std::max(loudest, level);
	}
	EXPECT_LT(quietest, loudest * 0.25f);

	definition.lfo.delay = 99;
	samples = render(definition, 69, 1.0f, s_rate);
	EXPECT_NEAR(rms(samples, 0, s_rate / 2), rms(samples, s_rate / 2, s_rate), 0.01f);
}

TEST(FmVoice, EveryLfoWaveStaysBounded)
{
	for (const auto wave : { FmLfoWave::Triangle, FmLfoWave::SawDown, FmLfoWave::SawUp, FmLfoWave::Square,
		FmLfoWave::Sine, FmLfoWave::SampleHold })
	{
		auto definition{ single_carrier() };
		definition.lfo = { .wave = wave, .speed = 60, .pitchModDepth = 50, .ampModDepth = 50, .pitchModSensitivity = 4 };
		definition.operators[0].ampModSensitivity = 2;
		const auto samples{ render(definition, 69, 1.0f, s_rate / 2) };
		EXPECT_LE(peak(samples), 1.1f);
		EXPECT_GT(peak(samples), 0.1f);
	}
}

TEST(FmVoice, NoteOffRunsOperatorReleaseAndTheGate)
{
	auto definition{ single_carrier() };
	definition.envelope = { .attack = 0.0f, .decay = 0.0f, .sustain = 1.0f, .release = 0.01f };
	FmInstrumentVoice voice{ definition, 69, 1.0f, s_rate };
	ASSERT_TRUE(voice.valid());

	float sample{};
	for (int i{}; i < 4800; ++i)
		ASSERT_TRUE(voice.next(sample));
	EXPECT_FALSE(voice.finished());

	voice.note_off();
	for (int i{}; i < 480; ++i)
		ASSERT_TRUE(voice.next(sample));
	EXPECT_FALSE(voice.next(sample));
	EXPECT_TRUE(voice.finished());
	EXPECT_FLOAT_EQ(sample, 0.0f);
}

TEST(FmVoice, BlocksTheLowSidebandOfDetunedPairs)
{
	auto definition{ simple_pair(90) };
	definition.operators[0].detune = 4;
	const auto samples{ render(definition, 69, 1.0f, s_rate * 2) };
	double sum{};
	for (auto i{ s_rate }; i < s_rate * 2; ++i)
		sum += samples[i];
	EXPECT_LT(std::abs(sum / s_rate), 0.01);
	EXPECT_LT(tone_level(samples, 2.0), 0.02);
}

TEST(FmVoice, RenderIsDeterministic)
{
	FmInstrumentDefinition definition{};
	definition.algorithm = 5;
	definition.feedback = 6;
	definition.lfo = { .wave = FmLfoWave::SampleHold, .speed = 60, .pitchModDepth = 30, .pitchModSensitivity = 3 };
	EXPECT_EQ(render(definition, 60, 0.7f, 9600), render(definition, 60, 0.7f, 9600));
}

TEST(FmVoice, AdvanceMatchesRendering)
{
	FmInstrumentDefinition definition{};
	definition.algorithm = 1;
	definition.gain = 0.5f;
	definition.operators[0].envelope = { .rates = { 70, 45, 30, 60 }, .levels = { 99, 80, 60, 0 } };
	definition.operators[1].envelope = { .rates = { 80, 40, 30, 60 }, .levels = { 90, 60, 40, 0 } };
	definition.operators[1].outputLevel = 80;
	definition.operators[1].coarse = 2;
	definition.operators[2].outputLevel = 0;
	definition.operators[3].outputLevel = 0;
	definition.operators[4].outputLevel = 0;
	definition.operators[5].outputLevel = 0;

	for (const std::uint64_t frames : { std::uint64_t{ 1 }, std::uint64_t{ 15 }, std::uint64_t{ 16 }, std::uint64_t{ 17 },
		std::uint64_t{ 4800 }, std::uint64_t{ 48007 } })
	{
		FmInstrumentVoice rendered{ definition, 64, 0.8f, s_rate };
		FmInstrumentVoice skipped{ definition, 64, 0.8f, s_rate };
		float sample{};
		for (std::uint64_t i{}; i < frames; ++i)
			ASSERT_TRUE(rendered.next(sample));
		skipped.advance(frames);

		// The DC filter restarts on a seek, so compare once it has settled.
		for (int i{}; i < 2400 + 256; ++i)
		{
			float expected{};
			float actual{};
			ASSERT_TRUE(rendered.next(expected));
			ASSERT_TRUE(skipped.next(actual));
			if (i >= 2400)
				ASSERT_NEAR(actual, expected, 0.002f) << "after " << frames << " frames, sample " << i;
		}
	}
}

TEST(FmVoice, AdvanceFollowsTheEnvelopeThroughRelease)
{
	auto definition{ single_carrier() };
	definition.envelope = { .attack = 0.0f, .decay = 0.0f, .sustain = 1.0f, .release = 0.05f };
	FmInstrumentVoice voice{ definition, 69, 1.0f, s_rate };
	voice.advance(s_rate);
	voice.note_off();
	voice.advance(s_rate);
	EXPECT_TRUE(voice.finished());
}

TEST(FmVoice, EnvelopeTimingIsIndependentOfSampleRate)
{
	auto definition{ single_carrier() };
	definition.operators[0].envelope = { .rates = { 99, 45, 99, 99 }, .levels = { 99, 0, 0, 0 } };
	const auto level{ [&](std::uint32_t rate)
	{
		const auto samples{ render(definition, 69, 1.0f, rate, rate) };
		return rms(samples, rate / 2, rate);
	} };
	EXPECT_NEAR(level(44100) / level(96000), 1.0f, 0.05f);
}

TEST(FmVoice, RejectsInvalidDefinitions)
{
	const auto rejected{ [](const auto& change)
	{
		FmInstrumentDefinition definition{};
		change(definition);
		return !FmInstrumentVoice{ definition, 60, 1.0f, s_rate }.valid();
	} };
	EXPECT_FALSE(rejected([](FmInstrumentDefinition&) {}));
	EXPECT_TRUE(rejected([](auto& d) { d.algorithm = 0; }));
	EXPECT_TRUE(rejected([](auto& d) { d.algorithm = 33; }));
	EXPECT_TRUE(rejected([](auto& d) { d.feedback = 8; }));
	EXPECT_TRUE(rejected([](auto& d) { d.transpose = 25; }));
	EXPECT_TRUE(rejected([](auto& d) { d.gain = 5.0f; }));
	EXPECT_TRUE(rejected([](auto& d) { d.outputTrim = 0.5f; }));
	EXPECT_TRUE(rejected([](auto& d) { d.gain = std::nanf(""); }));
	EXPECT_TRUE(rejected([](auto& d) { d.envelope.release = -1.0f; }));
	EXPECT_TRUE(rejected([](auto& d) { d.pitchEnvelope.levels[0] = 100; }));
	EXPECT_TRUE(rejected([](auto& d) { d.lfo.speed = 100; }));
	EXPECT_TRUE(rejected([](auto& d) { d.lfo.wave = static_cast<FmLfoWave>(6); }));
	EXPECT_TRUE(rejected([](auto& d) { d.operators[5].outputLevel = 100; }));
	EXPECT_TRUE(rejected([](auto& d) { d.operators[2].coarse = 32; }));
	EXPECT_TRUE(rejected([](auto& d) { d.operators[1].detune = 8; }));
	EXPECT_TRUE(rejected([](auto& d) { d.operators[0].envelope.rates[3] = 100; }));
	EXPECT_TRUE(rejected([](auto& d) { d.operators[0].levelScalingLeftCurve = static_cast<FmScalingCurve>(4); }));

	const FmInstrumentDefinition definition{};
	EXPECT_FALSE((FmInstrumentVoice{ definition, 128, 1.0f, s_rate }.valid()));
	EXPECT_FALSE((FmInstrumentVoice{ definition, 60, 0.0f, s_rate }.valid()));
	EXPECT_FALSE((FmInstrumentVoice{ definition, 60, 1.5f, s_rate }.valid()));
	EXPECT_FALSE((FmInstrumentVoice{ definition, 60, 1.0f, 4000 }.valid()));
}

TEST(FmVoice, RenderedSamplesAreBounded)
{
	FmInstrumentDefinition definition{};
	definition.algorithm = 32;
	definition.gain = 2.0f;
	std::string error{};
	const auto rendered{ render_fm_sample(definition, 60, 1.0f, 0.1f, 22050, error) };
	ASSERT_TRUE(rendered) << error;
	EXPECT_EQ(rendered->sampleRate, 22050u);
	EXPECT_EQ(rendered->channels, 1u);
	EXPECT_LE(peak(rendered->samples), 0.95f);
	EXPECT_FALSE(render_fm_sample(definition, 60, 1.0f, 40.0f, 22050, error));
	definition.algorithm = 0;
	EXPECT_FALSE(render_fm_sample(definition, 60, 1.0f, 0.1f, 22050, error));
}

// Not a check: run with --gtest_also_run_disabled_tests in release to see the worst-case cost of the mix budget.
TEST(FmVoice, DISABLED_WorstCaseCost)
{
	FmInstrumentDefinition definition{};
	definition.algorithm = 1;
	definition.feedback = 6;
	definition.lfo = { .wave = FmLfoWave::Sine, .speed = 35, .pitchModDepth = 10, .pitchModSensitivity = 3 };
	constexpr std::size_t voices{ 56 };
	constexpr std::size_t seconds{ 4 };
	std::vector<FmInstrumentVoice> pool{};
	for (std::size_t i{}; i < voices; ++i)
		pool.emplace_back(definition, static_cast<std::uint8_t>(40 + i % 40), 0.8f, s_rate);

	float sum{};
	const auto start{ std::chrono::steady_clock::now() };
	for (std::size_t frame{}; frame < seconds * s_rate; ++frame)
		for (auto& voice : pool)
		{
			float sample{};
			voice.next(sample);
			sum += sample;
		}
	const std::chrono::duration<double> elapsed{ std::chrono::steady_clock::now() - start };
	std::printf("%zu voices: %.2f%% of one core (checksum %f)\n", voices,
		elapsed.count() / static_cast<double>(seconds) * 100.0, static_cast<double>(sum));
}
