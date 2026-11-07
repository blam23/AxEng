#include "axeng/core/audio/fm_voice.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <numbers>

namespace
{
	// Levels are in steps of 0.75 dB, so 8 steps halve the amplitude. 127 is full scale.
	constexpr float s_full_level{ 127.0f };
	constexpr float s_steps_per_octave{ 8.0f };
	constexpr float s_silent_gain{ 2.0e-5f };

	constexpr std::uint32_t s_block_frames{ 16 };
	constexpr float s_dc_block_hz{ 12.0f };
	constexpr std::uint32_t s_sine_bits{ 12 };
	constexpr std::uint32_t s_sine_size{ 1u << s_sine_bits };
	constexpr std::uint32_t s_sine_fraction_bits{ 32 - s_sine_bits };
	constexpr float s_sine_fraction_scale{ 1.0f / static_cast<float>(1u << s_sine_fraction_bits) };
	constexpr float s_phase_scale{ 4294967296.0f };
	// A full-level modulator moves the phase by two cycles: a modulation index of about 13.
	constexpr float s_modulation_scale{ 2.0f * s_phase_scale };

	constexpr float s_envelope_reference_rate{ 44100.0f };
	constexpr float s_envelope_steps_scale{ 1.0f / 2097152.0f };
	constexpr float s_attack_jump_attenuation{ 67.0f };
	constexpr float s_attack_curve_offset{ 15.0f };

	constexpr float s_detune_cents_per_step{ 1.0f };
	constexpr float s_lfo_amp_attenuation{ 32.0f };
	constexpr double s_lfo_hz_per_unit{ 0.00586 };
	constexpr float s_lfo_max_delay_seconds{ 5.0f };
	constexpr float s_pitch_envelope_range_semitones{ 48.0f };

	constexpr std::array<std::uint8_t, 20> s_low_level_scale{
		0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46 };
	constexpr std::array<std::uint8_t, 8> s_pitch_mod_sensitivity{ 0, 10, 20, 33, 55, 92, 153, 255 };
	constexpr std::array<std::uint8_t, 4> s_amp_mod_sensitivity{ 0, 66, 109, 255 };

	template <typename... Numbers>
	constexpr std::uint8_t ops(Numbers... numbers) noexcept
	{
		return static_cast<std::uint8_t>(((1u << (numbers - 1)) | ...));
	}

	constexpr ax::audio::FmAlgorithm route(std::array<std::uint8_t, ax::audio::fm_operator_count> modulators,
		std::uint8_t carriers, int feedbackTarget, int feedbackSource = 0) noexcept
	{
		return { .modulators = modulators, .carriers = carriers,
			.feedbackTarget = static_cast<std::uint8_t>(feedbackTarget - 1),
			.feedbackSource = static_cast<std::uint8_t>((feedbackSource == 0 ? feedbackTarget : feedbackSource) - 1) };
	}

	constexpr std::array<ax::audio::FmAlgorithm, ax::audio::fm_algorithm_count> s_algorithms{
		route({ ops(2), 0, ops(4), ops(5), ops(6), 0 }, ops(1, 3), 6),
		route({ ops(2), 0, ops(4), ops(5), ops(6), 0 }, ops(1, 3), 2),
		route({ ops(2), ops(3), 0, ops(5), ops(6), 0 }, ops(1, 4), 6),
		route({ ops(2), ops(3), 0, ops(5), ops(6), 0 }, ops(1, 4), 6, 4),
		route({ ops(2), 0, ops(4), 0, ops(6), 0 }, ops(1, 3, 5), 6),
		route({ ops(2), 0, ops(4), 0, ops(6), 0 }, ops(1, 3, 5), 6, 5),
		route({ ops(2), 0, ops(4, 5), 0, ops(6), 0 }, ops(1, 3), 6),
		route({ ops(2), 0, ops(4, 5), 0, ops(6), 0 }, ops(1, 3), 4),
		route({ ops(2), 0, ops(4, 5), 0, ops(6), 0 }, ops(1, 3), 2),
		route({ ops(2), ops(3), 0, ops(5, 6), 0, 0 }, ops(1, 4), 3),
		route({ ops(2), ops(3), 0, ops(5, 6), 0, 0 }, ops(1, 4), 6),
		route({ ops(2), 0, ops(4, 5, 6), 0, 0, 0 }, ops(1, 3), 2),
		route({ ops(2), 0, ops(4, 5, 6), 0, 0, 0 }, ops(1, 3), 6),
		route({ ops(2), 0, ops(4), ops(5, 6), 0, 0 }, ops(1, 3), 6),
		route({ ops(2), 0, ops(4), ops(5, 6), 0, 0 }, ops(1, 3), 2),
		route({ ops(2, 3, 5), 0, ops(4), 0, ops(6), 0 }, ops(1), 6),
		route({ ops(2, 3, 5), 0, ops(4), 0, ops(6), 0 }, ops(1), 2),
		route({ ops(2, 3, 4), 0, 0, ops(5), ops(6), 0 }, ops(1), 3),
		route({ ops(2), ops(3), 0, ops(6), ops(6), 0 }, ops(1, 4, 5), 6),
		route({ ops(3), ops(3), 0, ops(5, 6), 0, 0 }, ops(1, 2, 4), 3),
		route({ ops(3), ops(3), 0, ops(6), ops(6), 0 }, ops(1, 2, 4, 5), 3),
		route({ ops(2), 0, ops(6), ops(6), ops(6), 0 }, ops(1, 3, 4, 5), 6),
		route({ 0, ops(3), 0, ops(6), ops(6), 0 }, ops(1, 2, 4, 5), 6),
		route({ 0, 0, ops(6), ops(6), ops(6), 0 }, ops(1, 2, 3, 4, 5), 6),
		route({ 0, 0, 0, ops(6), ops(6), 0 }, ops(1, 2, 3, 4, 5), 6),
		route({ 0, ops(3), 0, ops(5, 6), 0, 0 }, ops(1, 2, 4), 6),
		route({ 0, ops(3), 0, ops(5, 6), 0, 0 }, ops(1, 2, 4), 3),
		route({ ops(2), 0, ops(4), ops(5), 0, 0 }, ops(1, 3, 6), 5),
		route({ 0, 0, ops(4), 0, ops(6), 0 }, ops(1, 2, 3, 5), 6),
		route({ 0, 0, ops(4), ops(5), 0, 0 }, ops(1, 2, 3, 6), 5),
		route({ 0, 0, 0, 0, ops(6), 0 }, ops(1, 2, 3, 4, 5), 6),
		route({ 0, 0, 0, 0, 0, 0 }, ops(1, 2, 3, 4, 5, 6), 6),
	};

	const float* sine_table() noexcept
	{
		static const auto table{ []
		{
			std::array<float, s_sine_size + 1> values{};
			for (std::uint32_t i{}; i <= s_sine_size; ++i)
				values[i] = static_cast<float>(std::sin(2.0 * std::numbers::pi * i / s_sine_size));
			return values;
		}() };
		return table.data();
	}

	float sine_at(const float* table, std::uint32_t phase) noexcept
	{
		const auto index{ phase >> s_sine_fraction_bits };
		const auto fraction{ static_cast<float>(phase & ((1u << s_sine_fraction_bits) - 1)) * s_sine_fraction_scale };
		const auto a{ table[index] };
		return a + (table[index + 1] - a) * fraction;
	}

	std::uint32_t phase_offset(float modulation) noexcept
	{
		return static_cast<std::uint32_t>(static_cast<std::int64_t>(modulation * s_modulation_scale));
	}

	float scaled_level(std::uint8_t level) noexcept
	{
		return level < s_low_level_scale.size() ? static_cast<float>(s_low_level_scale[level])
			: static_cast<float>(level) + 28.0f;
	}

	float attenuation_gain(float attenuation) noexcept
	{
		return std::exp2(-attenuation / s_steps_per_octave);
	}

	float scaling_curve(std::uint32_t group, std::uint8_t depth, ax::audio::FmScalingCurve curve) noexcept
	{
		using ax::audio::FmScalingCurve;
		const auto linear{ curve == FmScalingCurve::NegativeLinear || curve == FmScalingCurve::PositiveLinear };
		const auto span{ static_cast<float>(std::min(group, 32u)) };
		const auto shape{ linear ? span : (std::exp2(span / 6.0f) - 1.0f) / (std::exp2(32.0f / 6.0f) - 1.0f) * 31.25f };
		const auto scale{ shape * static_cast<float>(depth) * 0.08f };
		const auto negative{ curve == FmScalingCurve::NegativeLinear || curve == FmScalingCurve::NegativeExponential };
		return negative ? -scale : scale;
	}

	float level_scaling(const ax::audio::FmOperator& op, int key) noexcept
	{
		const auto distance{ key - (static_cast<int>(op.levelScalingBreakpoint) + 21) };
		const auto group{ static_cast<std::uint32_t>(std::abs(distance)) / 3 };
		return distance >= 0 ? scaling_curve(group, op.levelScalingRightDepth, op.levelScalingRightCurve)
			: scaling_curve(group, op.levelScalingLeftDepth, op.levelScalingLeftCurve);
	}

	float velocity_scaling(std::uint8_t sensitivity, float velocity) noexcept
	{
		return static_cast<float>(sensitivity) * (255.0f * std::pow(velocity, 0.33f) - 239.0f) / 16.0f;
	}

	// Envelope speed in level steps per second. It doubles every four quantized rates.
	float envelope_steps_per_second(std::uint8_t rate, std::uint8_t rateScaling, int key) noexcept
	{
		const auto keyOffset{ std::clamp(key / 3 - 7, 0, 31) };
		const auto quantized{ std::min(rate * 41 / 64 + ((rateScaling * keyOffset) >> 3), 63) };
		const auto mantissa{ static_cast<float>(4 + (quantized & 3)) };
		return std::ldexp(mantissa, 2 + (quantized >> 2)) * s_envelope_steps_scale * s_envelope_reference_rate;
	}

	float pitch_envelope_semitones(std::uint8_t level) noexcept
	{
		const auto position{ std::clamp((static_cast<float>(level) - 50.0f) / 49.0f, -1.0f, 1.0f) };
		return s_pitch_envelope_range_semitones * position * std::abs(position);
	}

	double lfo_hz(std::uint8_t speed) noexcept
	{
		const auto unit{ std::max(1.0, static_cast<double>(speed) * 2.578) };
		return unit * (unit < 160.0 ? 11.0 : 11.0 + (unit - 160.0) / 16.0) * s_lfo_hz_per_unit;
	}

	bool valid_envelope(const ax::audio::FmEnvelope& envelope) noexcept
	{
		const auto valid_value{ [](std::uint8_t value) { return value <= 99; } };
		return std::ranges::all_of(envelope.rates, valid_value) && std::ranges::all_of(envelope.levels, valid_value);
	}

	bool valid_operator(const ax::audio::FmOperator& op) noexcept
	{
		const auto last_curve{ static_cast<std::uint8_t>(ax::audio::FmScalingCurve::PositiveLinear) };
		return valid_envelope(op.envelope) && op.outputLevel <= 99 && op.coarse <= 31 && op.fine <= 99
			&& op.detune >= -7 && op.detune <= 7 && op.velocitySensitivity <= 7 && op.ampModSensitivity <= 3
			&& op.rateScaling <= 7 && op.levelScalingBreakpoint <= 99 && op.levelScalingLeftDepth <= 99
			&& op.levelScalingRightDepth <= 99
			&& static_cast<std::uint8_t>(op.levelScalingLeftCurve) <= last_curve
			&& static_cast<std::uint8_t>(op.levelScalingRightCurve) <= last_curve;
	}

	bool valid_definition(const ax::audio::FmInstrumentDefinition& definition) noexcept
	{
		const auto& lfo{ definition.lfo };
		return definition.algorithm >= 1 && definition.algorithm <= ax::audio::fm_algorithm_count
			&& definition.feedback <= 7 && definition.transpose >= -24 && definition.transpose <= 24
			&& std::isfinite(definition.gain) && definition.gain >= 0.0f && definition.gain <= 4.0f
			&& std::isfinite(definition.outputTrim) && definition.outputTrim >= 1.0f && definition.outputTrim <= 16.0f
			&& valid_envelope(definition.pitchEnvelope)
			&& static_cast<std::uint8_t>(lfo.wave) <= static_cast<std::uint8_t>(ax::audio::FmLfoWave::SampleHold)
			&& lfo.speed <= 99 && lfo.delay <= 99 && lfo.pitchModDepth <= 99 && lfo.ampModDepth <= 99
			&& lfo.pitchModSensitivity <= 7
			&& std::ranges::all_of(definition.operators, valid_operator);
	}

	float operator_frequency(const ax::audio::FmOperator& op, float keyFrequency) noexcept
	{
		if (op.fixedFrequency)
		{
			const auto decade{ static_cast<float>(op.coarse & 3) };
			return std::pow(10.0f, decade + static_cast<float>(op.fine) / 100.0f);
		}
		const auto multiple{ op.coarse == 0 ? 0.5f : static_cast<float>(op.coarse) };
		return keyFrequency * multiple * (1.0f + static_cast<float>(op.fine) / 100.0f);
	}

	float hashed_unit(std::uint32_t value) noexcept
	{
		value = (value ^ (value >> 16)) * 0x7feb352du;
		value = (value ^ (value >> 15)) * 0x846ca68bu;
		value ^= value >> 16;
		return static_cast<float>(value >> 8) / 8388607.5f - 1.0f;
	}

	// Moves level linearly towards target and returns the blocks left over once it arrives.
	std::uint64_t slew(float& level, float target, float step, std::uint64_t blocks) noexcept
	{
		const auto distance{ std::abs(target - level) };
		const auto needed{ static_cast<std::uint64_t>(std::ceil(distance / step)) };
		if (blocks < needed)
		{
			level += (target > level ? step : -step) * static_cast<float>(blocks);
			return 0;
		}
		level = target;
		return blocks - needed;
	}
}

const ax::audio::FmAlgorithm* ax::audio::fm_algorithm(std::uint8_t algorithm) noexcept
{
	return algorithm >= 1 && algorithm <= fm_algorithm_count ? &s_algorithms[algorithm - 1] : nullptr;
}

ax::audio::FmInstrumentVoice::FmInstrumentVoice(const FmInstrumentDefinition& definition, std::uint8_t note,
	float velocity, std::uint32_t sampleRate) noexcept
	: m_sampleRate{ sampleRate }
{
	m_valid = valid_definition(definition) && note <= 127 && sampleRate >= 8000 && sampleRate <= 192000
		&& std::isfinite(velocity) && velocity > 0.0f && velocity <= 1.0f;
	if (!m_valid)
		return;

	// Velocity reaches the sound through each operator's sensitivity, so it can change brightness as well as level.
	m_envelope.start(definition.envelope, 1.0f, sampleRate);
	m_valid = !m_envelope.finished();
	if (!m_valid)
		return;

	m_algorithm = fm_algorithm(definition.algorithm);
	m_sine = sine_table();
	m_gain = definition.gain * definition.outputTrim;
	m_blockSeconds = static_cast<double>(s_block_frames) / static_cast<double>(sampleRate);
	m_feedbackScale = definition.feedback == 0 ? 0.0f : std::ldexp(0.5f, static_cast<int>(definition.feedback) - 8);

	const auto key{ std::clamp(static_cast<int>(note) + definition.transpose, 0, 127) };
	const auto keyFrequency{ 440.0f * std::exp2((static_cast<float>(key) - 69.0f) / 12.0f) };
	const auto blockSeconds{ static_cast<float>(m_blockSeconds) };
	for (std::size_t i{}; i < fm_operator_count; ++i)
	{
		const auto& source{ definition.operators[i] };
		auto& op{ m_operators[i] };
		const auto output{ std::clamp(scaled_level(source.outputLevel) + level_scaling(source, key)
			+ velocity_scaling(source.velocitySensitivity, velocity), 0.0f, s_full_level) };
		op.outputAttenuation = s_full_level - output;
		op.ampModScale = static_cast<float>(s_amp_mod_sensitivity[source.ampModSensitivity]) / 255.0f;
		op.fixedFrequency = source.fixedFrequency;
		op.frequency = operator_frequency(source, keyFrequency);
		if (!source.fixedFrequency && source.detune != 0)
			op.frequency *= std::exp2(static_cast<float>(source.detune) * s_detune_cents_per_step / 1200.0f);
		for (std::size_t stage{}; stage < 4; ++stage)
		{
			op.targets.levels[stage] = scaled_level(source.envelope.levels[stage]);
			op.targets.steps[stage] = envelope_steps_per_second(source.envelope.rates[stage], source.rateScaling, key)
				* blockSeconds;
		}
		op.level = op.targets.levels[3];
	}

	// Averaging the sounding carriers keeps gain close to the voice's peak level, whatever the algorithm.
	int carriers{};
	for (std::size_t i{}; i < fm_operator_count; ++i)
		carriers += ((m_algorithm->carriers >> i) & 1u) != 0 && definition.operators[i].outputLevel > 0;
	m_gain /= static_cast<float>(std::max(carriers, 1));
	m_dcBlock = 1.0f - 2.0f * std::numbers::pi_v<float> * s_dc_block_hz / static_cast<float>(sampleRate);

	for (std::size_t stage{}; stage < 4; ++stage)
	{
		m_pitchTargets.levels[stage] = pitch_envelope_semitones(definition.pitchEnvelope.levels[stage]);
		m_pitchTargets.steps[stage] = 0.1f * std::exp2(static_cast<float>(definition.pitchEnvelope.rates[stage]) / 7.5f)
			* blockSeconds;
	}
	m_pitchSemitones = m_pitchTargets.levels[3];

	const auto& lfo{ definition.lfo };
	m_lfoWave = lfo.wave;
	m_lfoSeed = note;
	m_lfoCycleStep = lfo_hz(lfo.speed) * m_blockSeconds;
	const auto delay{ static_cast<float>(lfo.delay) / 99.0f };
	m_lfoDelaySeconds = delay * delay * s_lfo_max_delay_seconds;
	m_lfoFadeSeconds = m_lfoDelaySeconds * 0.5;
	m_lfoPitchSemitones = 12.0f * static_cast<float>(lfo.pitchModDepth) / 99.0f
		* static_cast<float>(s_pitch_mod_sensitivity[lfo.pitchModSensitivity]) / 255.0f;
	m_lfoAmpAttenuation = s_lfo_amp_attenuation * static_cast<float>(lfo.ampModDepth) / 99.0f;
	update_increments(m_pitchSemitones);
}

void ax::audio::FmInstrumentVoice::note_off() noexcept
{
	if (!m_valid)
		return;
	m_envelope.note_off();
	for (auto& op : m_operators)
		if (op.stage != Stage::Done)
			op.stage = Stage::Release;
	if (m_pitchStage != Stage::Done)
		m_pitchStage = Stage::Release;
}

ax::audio::FmInstrumentVoice::Stage ax::audio::FmInstrumentVoice::next_stage(Stage stage) noexcept
{
	return stage == Stage::First ? Stage::Second : stage == Stage::Second ? Stage::Third
		: stage == Stage::Third ? Stage::Sustain : Stage::Done;
}

void ax::audio::FmInstrumentVoice::step_operator(OperatorState& op, std::uint64_t blocks) noexcept
{
	while (blocks > 0 && op.stage != Stage::Sustain && op.stage != Stage::Done)
	{
		const auto index{ op.stage == Stage::Release ? std::size_t{ 3 } : static_cast<std::size_t>(op.stage) };
		const auto target{ op.targets.levels[index] };
		const auto step{ op.targets.steps[index] };
		if (op.level < target)
		{
			// Attacks follow a curve in level steps, so they start fast and slow as they near full level.
			const auto jump{ s_full_level - op.outputAttenuation - s_attack_jump_attenuation };
			op.level = std::max(op.level, std::min(jump, target));
			const auto attenuation{ s_full_level - op.level + op.outputAttenuation };
			op.level = std::min(target, op.level + (s_attack_curve_offset + attenuation) / s_steps_per_octave * step);
			--blocks;
		}
		else
			blocks = slew(op.level, target, step, blocks);
		if (op.level == target)
			op.stage = next_stage(op.stage);
	}
}

void ax::audio::FmInstrumentVoice::step_pitch(std::uint64_t blocks) noexcept
{
	while (blocks > 0 && m_pitchStage != Stage::Sustain && m_pitchStage != Stage::Done)
	{
		const auto index{ m_pitchStage == Stage::Release ? std::size_t{ 3 } : static_cast<std::size_t>(m_pitchStage) };
		blocks = slew(m_pitchSemitones, m_pitchTargets.levels[index], m_pitchTargets.steps[index], blocks);
		if (m_pitchSemitones == m_pitchTargets.levels[index])
			m_pitchStage = next_stage(m_pitchStage);
	}
}

void ax::audio::FmInstrumentVoice::step_lfo(std::uint64_t blocks) noexcept
{
	const auto count{ static_cast<double>(blocks) };
	m_elapsedSeconds += m_blockSeconds * count;
	m_lfoPhase += m_lfoCycleStep * count;
	const auto wraps{ std::floor(m_lfoPhase) };
	m_lfoPhase -= wraps;
	m_lfoCycle += static_cast<std::uint32_t>(wraps);
}

float ax::audio::FmInstrumentVoice::lfo_value() const noexcept
{
	const auto phase{ static_cast<float>(m_lfoPhase) };
	switch (m_lfoWave)
	{
	case FmLfoWave::Triangle:
		return 1.0f - 4.0f * std::abs(phase - 0.5f);
	case FmLfoWave::SawDown:
		return 1.0f - 2.0f * phase;
	case FmLfoWave::SawUp:
		return 2.0f * phase - 1.0f;
	case FmLfoWave::Square:
		return phase < 0.5f ? 1.0f : -1.0f;
	case FmLfoWave::Sine:
		return std::sin(phase * 2.0f * std::numbers::pi_v<float>);
	case FmLfoWave::SampleHold:
		return hashed_unit(m_lfoCycle * 131u + m_lfoSeed);
	}
	return 0.0f;
}

float ax::audio::FmInstrumentVoice::lfo_fade() const noexcept
{
	if (m_elapsedSeconds < m_lfoDelaySeconds)
		return 0.0f;
	if (m_lfoFadeSeconds <= 0.0)
		return 1.0f;
	return static_cast<float>(std::min(1.0, (m_elapsedSeconds - m_lfoDelaySeconds) / m_lfoFadeSeconds));
}

void ax::audio::FmInstrumentVoice::update_increments(float semitones) noexcept
{
	const auto ratio{ semitones == 0.0f ? 1.0f : std::exp2(semitones / 12.0f) };
	const auto scale{ s_phase_scale / static_cast<float>(m_sampleRate) };
	for (auto& op : m_operators)
	{
		const auto frequency{ op.fixedFrequency ? op.frequency : op.frequency * ratio };
		op.increment = static_cast<std::uint32_t>(static_cast<std::int64_t>(frequency * scale));
	}
}

void ax::audio::FmInstrumentVoice::update_control() noexcept
{
	step_pitch(1);
	step_lfo(1);
	const auto lfo{ lfo_value() };
	const auto fade{ lfo_fade() };
	update_increments(m_pitchSemitones + lfo * fade * m_lfoPitchSemitones);
	const auto ampDepth{ (lfo + 1.0f) * 0.5f * fade * m_lfoAmpAttenuation };
	for (auto& op : m_operators)
	{
		step_operator(op, 1);
		const auto attenuation{ s_full_level - op.level + op.outputAttenuation + ampDepth * op.ampModScale };
		const auto target{ attenuation_gain(attenuation) };
		op.active = std::max(op.gain, target) > s_silent_gain;
		if (!op.active)
			op.gain = target;
		op.gainStep = op.active ? (target - op.gain) / static_cast<float>(s_block_frames) : 0.0f;
	}
	m_blockRemaining = s_block_frames;
}

void ax::audio::FmInstrumentVoice::skip_samples(std::uint32_t count) noexcept
{
	for (auto& op : m_operators)
	{
		op.phase += op.increment * count;
		op.gain += op.gainStep * static_cast<float>(count);
	}
	m_blockRemaining -= count;
}

void ax::audio::FmInstrumentVoice::advance(std::uint64_t frames) noexcept
{
	if (!m_valid || frames == 0)
		return;

	m_envelope.advance(frames);
	m_feedbackNewest = 0.0f;
	m_feedbackOldest = 0.0f;
	m_dcPrimed = false;
	const auto current{ static_cast<std::uint32_t>(std::min<std::uint64_t>(frames, m_blockRemaining)) };
	skip_samples(current);
	frames -= current;

	const auto blocks{ frames / s_block_frames };
	const auto remainder{ static_cast<std::uint32_t>(frames % s_block_frames) };
	if (blocks > 0)
	{
		const auto skipped{ blocks - 1 };
		step_pitch(skipped);
		step_lfo(skipped);
		for (auto& op : m_operators)
		{
			step_operator(op, skipped);
			op.phase += op.increment * static_cast<std::uint32_t>(skipped * s_block_frames);
		}
		update_control();
		skip_samples(s_block_frames);
	}
	if (remainder > 0)
	{
		update_control();
		skip_samples(remainder);
	}
}

bool ax::audio::FmInstrumentVoice::next(float& sample) noexcept
{
	if (!m_valid || m_envelope.finished())
	{
		sample = 0.0f;
		return false;
	}

	if (m_blockRemaining == 0)
		update_control();
	--m_blockRemaining;

	const auto& algorithm{ *m_algorithm };
	for (auto index{ fm_operator_count }; index-- > 0;)
	{
		auto& op{ m_operators[index] };
		if (op.active)
		{
			auto modulation{ index == algorithm.feedbackTarget
				? (m_feedbackNewest + m_feedbackOldest) * m_feedbackScale : 0.0f };
			for (std::uint32_t mask{ algorithm.modulators[index] }; mask != 0; mask &= mask - 1)
				modulation += m_operators[static_cast<std::size_t>(std::countr_zero(mask))].output;
			op.output = sine_at(m_sine, op.phase + phase_offset(modulation)) * op.gain;
			op.gain += op.gainStep;
		}
		else
			op.output = 0.0f;
		op.phase += op.increment;
	}

	m_feedbackOldest = m_feedbackNewest;
	m_feedbackNewest = m_operators[algorithm.feedbackSource].output;

	float mixed{};
	for (std::uint32_t mask{ algorithm.carriers }; mask != 0; mask &= mask - 1)
		mixed += m_operators[static_cast<std::size_t>(std::countr_zero(mask))].output;
	// A modulator at the carrier's frequency puts a sideband at or near 0 Hz, which would only eat headroom.
	if (!m_dcPrimed)
	{
		// After a seek, resume as if the filter had settled so that it adds no transient.
		m_dcInput = mixed;
		m_dcOutput = mixed;
		m_dcPrimed = true;
	}
	m_dcOutput = mixed - m_dcInput + m_dcBlock * m_dcOutput;
	m_dcInput = mixed;
	sample = m_dcOutput * m_envelope.next() * m_gain;
	return true;
}

std::optional<ax::audio::AudioSampleData> ax::audio::render_fm_sample(const FmInstrumentDefinition& definition,
	std::uint8_t note, float velocity, float holdSeconds, std::uint32_t sampleRate, std::string& error)
{
	FmInstrumentVoice voice{ definition, note, velocity, sampleRate };
	return render_voice_sample({ .holdSeconds = holdSeconds, .releaseSeconds = definition.envelope.release,
		.sampleRate = sampleRate, .voiceValid = voice.valid(),
		.next = [&voice](float& sample) { return voice.next(sample); },
		.noteOff = [&voice] { voice.note_off(); } }, error);
}
