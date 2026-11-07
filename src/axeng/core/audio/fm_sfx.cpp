#include "axeng/core/audio/fm_sfx.h"

#include "axeng/core/audio/sfx_rng.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
	using ax::audio::FmEnvelope;
	using ax::audio::FmOperator;
	using ax::audio::FmSfxParams;
	using Rng = ax::audio::detail::SfxRng;
	using Stages = std::array<std::uint8_t, 4>;

	constexpr std::uint32_t s_min_loop_bits{ 16 };
	constexpr std::uint32_t s_max_loop_bits{ 18 };
	// Feedback above this turns the operator into noise that never repeats exactly.
	constexpr std::uint8_t s_exact_feedback_limit{ 5 };

	std::uint8_t u8(int value) noexcept
	{
		return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
	}

	FmOperator make_op(int level, int coarse, int fine, Stages rates, Stages levels, int detune = 0) noexcept
	{
		FmOperator op{};
		op.outputLevel = u8(std::clamp(level, 0, 99));
		op.coarse = u8(coarse);
		op.fine = u8(std::clamp(fine, 0, 99));
		op.detune = static_cast<std::int8_t>(std::clamp(detune, -7, 7));
		op.envelope = FmEnvelope{ .rates = rates, .levels = levels };
		return op;
	}

	FmOperator silent_op() noexcept
	{
		return make_op(0, 1, 0, { 99, 99, 99, 99 }, { 0, 0, 0, 0 });
	}

	// Gains bring each family's peak to about 0.45, the level of the sfxr presets.
	FmSfxParams base(std::uint8_t algorithm, float gain, int note, float hold, float release) noexcept
	{
		FmSfxParams p{};
		p.patch.algorithm = algorithm;
		p.patch.gain = gain;
		p.patch.envelope = { .attack = 0.0f, .decay = 0.0f, .sustain = 1.0f, .release = release };
		p.patch.operators.fill(silent_op());
		p.note = u8(std::clamp(note, 0, 127));
		p.hold = hold;
		return p;
	}

	// Glass-like bell: three carrier pairs with inharmonic modulators and a long ring.
	FmSfxParams make_chime(Rng& r)
	{
		// Separate statements: the order function arguments are evaluated in is unspecified.
		const auto note{ r.between(79, 88) };
		auto p{ base(5, 0.9f, note, 0.05f, r.range(1.1f, 1.6f)) };
		const auto ring{ u8(r.between(38, 48)) };
		auto& ops{ p.patch.operators };
		ops[0] = make_op(99, 1, 0, { 99, ring, 30, 45 }, { 99, 75, 0, 0 });
		ops[1] = make_op(r.between(60, 74), 3, 50, { 99, u8(ring + 10), 35, 50 }, { 99, 50, 0, 0 });
		ops[2] = make_op(90, 1, 0, { 99, u8(ring + 2), 30, 45 }, { 99, 75, 0, 0 }, r.between(1, 3));
		ops[3] = make_op(r.between(48, 60), 7, 0, { 99, u8(ring + 14), 40, 50 }, { 99, 40, 0, 0 });
		ops[4] = make_op(r.between(70, 82), 3, 0, { 99, u8(ring + 6), 34, 50 }, { 99, 60, 0, 0 });
		ops[5] = make_op(r.between(45, 60), 1, 41, { 99, u8(ring + 12), 40, 50 }, { 99, 40, 0, 0 });
		return p;
	}

	// Short metallic strike with a gritty feedback chain.
	FmSfxParams make_clank(Rng& r)
	{
		const auto note{ r.between(45, 57) };
		auto p{ base(1, 0.67f, note, 0.03f, r.range(0.4f, 0.6f)) };
		p.patch.feedback = u8(r.between(5, 7));
		auto& ops{ p.patch.operators };
		const auto decay{ u8(r.between(56, 66)) };
		ops[0] = make_op(99, 1, r.between(0, 20), { 99, decay, 45, 60 }, { 99, 40, 0, 0 });
		ops[1] = make_op(r.between(80, 90), 1, 41, { 99, u8(decay + 8), 50, 60 }, { 99, 30, 0, 0 });
		ops[2] = make_op(88, 2, 38, { 99, u8(decay + 4), 48, 60 }, { 99, 35, 0, 0 });
		ops[3] = make_op(r.between(70, 80), 3, 17, { 99, u8(decay + 10), 50, 60 }, { 99, 30, 0, 0 });
		ops[4] = make_op(60, 5, 30, { 99, u8(decay + 12), 55, 60 }, { 99, 25, 0, 0 });
		ops[5] = make_op(60, 1, 0, { 99, u8(decay + 12), 55, 60 }, { 99, 25, 0, 0 });
		p.patch.pitchEnvelope = { .rates = { 72, 99, 99, 0 }, .levels = { 50, 50, 50, 55 } };
		return p;
	}

	// Two-tone siren: a square LFO swings the pitch of a steady tone.
	FmSfxParams make_alarm(Rng& r)
	{
		auto p{ base(5, 0.44f, r.between(72, 79), 0.8f, 0.08f) };
		auto& ops{ p.patch.operators };
		ops[0] = make_op(99, 1, 0, { 99, 99, 99, 70 }, { 99, 99, 99, 0 });
		const auto level{ r.between(70, 80) };
		ops[1] = make_op(level, r.chance(0.5f) ? 1 : 2, 0, { 99, 99, 99, 70 }, { 99, 99, 99, 0 });
		p.patch.lfo = { .wave = ax::audio::FmLfoWave::Square, .speed = u8(r.between(55, 70)),
			.pitchModDepth = u8(r.between(70, 90)), .pitchModSensitivity = 5 };
		return p;
	}

	// Falling laser zap with a gritty, self-modulating modulator.
	FmSfxParams make_zap(Rng& r)
	{
		auto p{ base(2, 0.45f, r.between(66, 76), 0.18f, 0.1f) };
		p.patch.feedback = u8(r.between(6, 7));
		auto& ops{ p.patch.operators };
		ops[0] = make_op(99, 1, 0, { 99, 45, 35, 60 }, { 99, 80, 0, 0 });
		ops[1] = make_op(r.between(85, 95), 1, r.between(0, 10), { 99, 55, 40, 60 }, { 99, 50, 0, 0 });
		const auto top{ u8(r.between(88, 95)) };
		const auto bottom{ u8(r.between(20, 30)) };
		const auto speed{ u8(r.between(80, 88)) };
		p.patch.pitchEnvelope = { .rates = { speed, 99, 99, 0 }, .levels = { bottom, bottom, bottom, top } };
		return p;
	}

	// Bright blip that glides up a few semitones.
	FmSfxParams make_confirm(Rng& r)
	{
		auto p{ base(5, 0.84f, r.between(72, 79), 0.12f, 0.35f) };
		auto& ops{ p.patch.operators };
		ops[0] = make_op(99, 1, 0, { 99, 50, 40, 55 }, { 99, 40, 0, 0 });
		ops[1] = make_op(r.between(60, 70), 2, 0, { 99, 60, 45, 60 }, { 99, 30, 0, 0 });
		ops[2] = make_op(70, 2, 0, { 99, 55, 40, 55 }, { 99, 35, 0, 0 });
		ops[3] = make_op(50, 1, 0, { 99, 60, 45, 60 }, { 99, 30, 0, 0 });
		const auto up{ u8(r.between(62, 66)) };
		p.patch.pitchEnvelope = { .rates = { u8(r.between(66, 74)), 99, 99, 0 }, .levels = { up, up, up, 50 } };
		return p;
	}

	// Low, buzzy refusal tone that sags a little.
	FmSfxParams make_deny(Rng& r)
	{
		auto p{ base(2, 0.85f, r.between(43, 50), 0.22f, 0.08f) };
		p.patch.feedback = 7;
		auto& ops{ p.patch.operators };
		ops[0] = make_op(99, 1, 0, { 99, 60, 50, 75 }, { 99, 90, 85, 0 });
		ops[1] = make_op(r.between(80, 90), 1, 0, { 99, 60, 50, 75 }, { 99, 90, 85, 0 });
		ops[2] = make_op(85, 0, 0, { 99, 60, 50, 75 }, { 99, 90, 85, 0 });
		ops[3] = make_op(60, 1, 0, { 99, 60, 50, 75 }, { 99, 90, 85, 0 });
		p.patch.pitchEnvelope = { .rates = { u8(r.between(55, 65)), 99, 99, 0 }, .levels = { 46, 46, 46, 52 } };
		return p;
	}

	// Machine hum: slightly detuned carriers beat slowly against each other.
	FmSfxParams make_hum(Rng& r)
	{
		auto p{ base(5, 0.77f, r.between(36, 43), 1.0f, 0.25f) };
		auto& ops{ p.patch.operators };
		const Stages rates{ 80, 99, 99, 60 };
		const Stages levels{ 99, 99, 99, 0 };
		const auto spread{ r.between(2, 4) };
		ops[0] = make_op(99, 1, 0, rates, levels, -spread);
		ops[1] = make_op(r.between(35, 48), 1, 0, rates, levels);
		ops[2] = make_op(95, 1, 0, rates, levels, spread);
		ops[3] = make_op(r.between(30, 42), 2, 0, rates, levels);
		ops[4] = make_op(r.between(65, 80), 2, 0, rates, levels, 1);
		ops[5] = make_op(r.between(25, 35), 3, 0, rates, levels);
		return p;
	}

	// Low drone whose brightness breathes with a slow LFO.
	FmSfxParams make_drone(Rng& r)
	{
		auto p{ base(5, 0.83f, r.between(31, 38), 1.5f, 0.4f) };
		auto& ops{ p.patch.operators };
		const Stages rates{ 60, 99, 99, 55 };
		const Stages levels{ 99, 99, 99, 0 };
		ops[0] = make_op(99, 1, 0, rates, levels);
		ops[0].ampModSensitivity = 1;
		ops[1] = make_op(r.between(55, 70), 1, 0, rates, levels);
		ops[1].ampModSensitivity = 3;
		ops[2] = make_op(70, 2, 0, rates, levels, 2);
		ops[3] = make_op(r.between(25, 35), 1, 0, rates, levels);
		p.patch.lfo = { .wave = ax::audio::FmLfoWave::Sine, .speed = u8(r.between(15, 30)),
			.ampModDepth = u8(r.between(40, 70)) };
		return p;
	}

	// Struck plate: inharmonic partials, the higher ones dying faster.
	FmSfxParams make_metal_hit(Rng& r)
	{
		const auto note{ r.between(60, 72) };
		auto p{ base(31, 1.3f, note, 0.05f, r.range(0.8f, 1.2f)) };
		p.patch.feedback = u8(r.between(3, 5));
		auto& ops{ p.patch.operators };
		const auto decay{ r.between(38, 44) };
		ops[0] = make_op(99, 1, 0, { 99, u8(decay), 30, 45 }, { 99, 60, 0, 0 });
		ops[1] = make_op(90, 2, 38, { 99, u8(decay + 8), 35, 50 }, { 99, 55, 0, 0 });
		ops[2] = make_op(84, 5, 8, { 99, u8(decay + 15), 40, 55 }, { 99, 50, 0, 0 });
		ops[3] = make_op(76, 8, 12, { 99, u8(decay + 22), 45, 60 }, { 99, 45, 0, 0 });
		ops[4] = make_op(70, 13, r.between(0, 30), { 99, u8(decay + 28), 50, 60 }, { 99, 40, 0, 0 });
		ops[5] = make_op(r.between(55, 65), 1, 41, { 99, u8(decay + 20), 45, 60 }, { 99, 40, 0, 0 });
		return p;
	}

	// Rising sweep that brightens as it climbs.
	FmSfxParams make_power_up(Rng& r)
	{
		auto p{ base(1, 0.79f, r.between(60, 67), 0.5f, 0.25f) };
		auto& ops{ p.patch.operators };
		const auto swell{ u8(r.between(35, 45)) };
		ops[0] = make_op(99, 1, 0, { 90, 99, 99, 60 }, { 99, 99, 99, 0 });
		ops[1] = make_op(r.between(70, 80), 1, 0, { swell, 99, 99, 60 }, { 99, 99, 99, 30 });
		ops[2] = make_op(75, 2, 0, { 90, 99, 99, 60 }, { 99, 99, 99, 0 });
		ops[3] = make_op(r.between(55, 65), 1, 0, { swell, 99, 99, 60 }, { 99, 99, 99, 30 });
		p.patch.pitchEnvelope = { .rates = { u8(r.between(62, 70)), 99, 99, 0 }, .levels = { 72, 72, 72, 30 } };
		return p;
	}

	using Generator = FmSfxParams (*)(Rng&);

	struct Preset
	{
		std::string_view name;
		Generator generate;
	};

	constexpr std::array<Preset, 10> s_presets
	{ {
		{ "chime", &make_chime },
		{ "clank", &make_clank },
		{ "alarm", &make_alarm },
		{ "zap", &make_zap },
		{ "confirm", &make_confirm },
		{ "deny", &make_deny },
		{ "hum", &make_hum },
		{ "drone", &make_drone },
		{ "metal_hit", &make_metal_hit },
		{ "power_up", &make_power_up },
	} };

	constexpr auto s_preset_names{ []
	{
		std::array<std::string_view, s_presets.size()> names{};
		for (std::size_t i{}; i < s_presets.size(); ++i)
			names[i] = s_presets[i].name;
		return names;
	}() };

	template <typename Value>
	void nudge(Rng& rng, Value& value, int low, int high, float amount, float span) noexcept
	{
		const auto delta{ rng.range(-amount, amount) * span };
		if (rng.chance(0.5f))
			value = static_cast<Value>(std::clamp(static_cast<int>(value) + static_cast<int>(std::lround(delta)), low, high));
	}
}

std::uint32_t ax::audio::fm_sfx_loop_frames(float loopLength) noexcept
{
	const auto frames{ std::isfinite(loopLength) && loopLength > 0.0f
		? static_cast<double>(loopLength) * sfx_sample_rate : 0.0 };
	const auto bits{ frames > 0.0 ? std::lround(std::log2(frames)) : 0L };
	return 1u << std::clamp<std::uint32_t>(static_cast<std::uint32_t>(std::max(bits, 0L)), s_min_loop_bits, s_max_loop_bits);
}

void ax::audio::clamp_fm_sfx(FmSfxParams& params) noexcept
{
	params.note = std::min<std::uint8_t>(params.note, 127);
	if (!std::isfinite(params.velocity) || !(params.velocity > 0.0f))
		params.velocity = 1.0f;
	params.velocity = std::min(params.velocity, 1.0f);
	params.hold = std::isfinite(params.hold) ? std::clamp(params.hold, 0.0f, sfx_max_length) : 0.2f;
	params.loopLength = std::isfinite(params.loopLength) ? std::clamp(params.loopLength, 1.0f, sfx_max_length) : 3.0f;
}

std::vector<float> ax::audio::render_fm_sfx(const FmSfxParams& input, const SynthOptions& options)
{
	auto params{ input };
	clamp_fm_sfx(params);
	FmInstrumentVoice voice{ params.patch, params.note, params.velocity, sfx_sample_rate };
	if (!voice.valid())
		return {};

	const auto maxSamples{ static_cast<std::size_t>(sfx_max_length * static_cast<float>(sfx_sample_rate)) };
	float sample{};
	if (!options.loop)
	{
		const auto holdFrames{ static_cast<std::size_t>(params.hold * static_cast<float>(sfx_sample_rate)) };
		std::vector<float> samples{};
		samples.reserve(std::min(maxSamples, holdFrames
			+ static_cast<std::size_t>(params.patch.envelope.release * static_cast<float>(sfx_sample_rate)) + 1));
		for (std::size_t i{}; i < maxSamples; ++i)
		{
			if (i == holdFrames)
				voice.note_off();
			if (!voice.next(sample))
				break;
			samples.push_back(std::clamp(sample, -1.0f, 1.0f));
		}
		// Avoid a click if the sound was cut off by the length cap.
		const auto fade{ std::min<std::size_t>(samples.size(), 64) };
		for (std::size_t i{}; i < fade; ++i)
			samples[samples.size() - 1 - i] *= static_cast<float>(i) / static_cast<float>(fade);
		return samples;
	}

	const auto loopFrames{ fm_sfx_loop_frames(params.loopLength) };
	voice.snap_to_loop(loopFrames);
	// Hold the note until its envelopes reach their sustain levels, then let the DC blocker and gain smoothing settle.
	for (std::size_t i{}; i < maxSamples && !voice.settled(); ++i)
		voice.next(sample);
	for (std::uint32_t i{}; i < sfx_sample_rate / 2; ++i)
		voice.next(sample);

	const auto& lfo{ params.patch.lfo };
	const bool exact{ params.patch.feedback <= s_exact_feedback_limit
		&& (lfo.pitchModDepth == 0 || lfo.pitchModSensitivity == 0) };
	const auto crossfade{ exact ? std::size_t{ 64 }
		: std::min<std::size_t>(loopFrames / 4, sfx_sample_rate * 15 / 100) };
	const auto search{ static_cast<std::size_t>(sfx_sample_rate / 200) };
	std::vector<float> rendered(loopFrames + search + std::max<std::size_t>(crossfade, 256));
	for (auto& value : rendered)
	{
		voice.next(sample);
		value = std::clamp(sample, -1.0f, 1.0f);
	}
	return make_seamless_loop(rendered, loopFrames, crossfade, search);
}

ax::audio::FmSfxParams ax::audio::mutate_fm(const FmSfxParams& params, float amount, std::uint64_t seed) noexcept
{
	auto result{ params };
	if (!(amount > 0.0f) || !std::isfinite(amount))
		return result;

	Rng rng{ seed };
	const auto* const algorithm{ fm_algorithm(result.patch.algorithm) };
	constexpr float level_span{ 49.5f };
	for (std::size_t i{}; i < fm_operator_count; ++i)
	{
		auto& op{ result.patch.operators[i] };
		const bool carrier{ algorithm == nullptr || ((algorithm->carriers >> i) & 1u) != 0 };
		if (op.outputLevel > 0)
			nudge(rng, op.outputLevel, 1, 99, amount, level_span);
		// Carriers set the pitch, so they only detune; modulators may also shift their ratio for a new timbre.
		if (!op.fixedFrequency && !carrier)
			nudge(rng, op.fine, 0, 99, amount, level_span);
		if (!op.fixedFrequency)
			nudge(rng, op.detune, -7, 7, amount, 7.0f);
		for (auto& rate : op.envelope.rates)
			nudge(rng, rate, 0, 99, amount, level_span);
		for (std::size_t stage{}; stage < 3; ++stage)
			if (op.envelope.levels[stage] > 0)
				nudge(rng, op.envelope.levels[stage], 1, 99, amount, level_span);
	}
	nudge(rng, result.patch.feedback, 0, 7, amount, 7.0f);
	const auto stretch{ rng.range(-amount, amount) };
	if (rng.chance(0.5f))
		result.hold *= 1.0f + stretch;
	clamp_fm_sfx(result);
	return result;
}

std::span<const std::string_view> ax::audio::fm_preset_names() noexcept
{
	return s_preset_names;
}

std::optional<ax::audio::FmSfxParams> ax::audio::generate_fm_preset(std::string_view preset, std::uint64_t seed) noexcept
{
	for (const auto& entry : s_presets)
	{
		if (entry.name == preset)
		{
			Rng rng{ seed };
			auto params{ entry.generate(rng) };
			clamp_fm_sfx(params);
			return params;
		}
	}
	return std::nullopt;
}
