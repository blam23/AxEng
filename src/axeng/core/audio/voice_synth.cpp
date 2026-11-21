#include "axeng/core/audio/voice_synth.h"

#include "axeng/core/audio/dsp.h"
#include "axeng/core/audio/sfx_rng.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace
{
	constexpr float s_open{ 0.4f };
	constexpr float s_close{ 0.16f };
	constexpr std::array<float, 3> s_formant_weights{ 1.0f, 0.6f, 0.35f };

	// Rosenberg's glottal flow over one cycle (phase 0..1): a raised-cosine opening, a quarter-cosine closing, then
	// closed.
	float glottal_flow(float phase) noexcept
	{
		if (phase < s_open) return 0.5f - 0.5f * std::cos(std::numbers::pi_v<float> * phase / s_open);
		if (phase < s_open + s_close) return std::cos(0.5f * std::numbers::pi_v<float> * (phase - s_open) / s_close);
		return 0.0f;
	}

	float lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }

	float smoothing_k(float seconds, float rate) noexcept
	{
		return seconds <= 0.0f ? 1.0f : 1.0f - std::exp(-1.0f / (seconds * rate));
	}

	// Leaves quiet samples alone and bends everything past 0.9 smoothly toward 1.
	float soft_clip(float x) noexcept
	{
		const auto a{ std::abs(x) };
		if (a <= 0.9f) return x;
		return std::copysign(0.9f + 0.1f * std::tanh((a - 0.9f) / 0.1f), x);
	}
}

ax::audio::RenderedVoice ax::audio::render_voice(const VoiceTimbre& timbre, std::span<const VoiceSegment> segments,
	std::uint32_t sampleRate, std::uint64_t seed)
{
	RenderedVoice out{};
	if (sampleRate == 0) return out;
	const auto rate{ static_cast<float>(sampleRate) };

	// Segment boundaries in samples, from the running total so rounding never drifts.
	std::vector<std::size_t> bounds{};
	bounds.reserve(segments.size() + 1);
	double elapsed{};
	bounds.push_back(0);
	out.starts.push_back(0.0f);
	for (const auto& segment : segments)
	{
		elapsed += std::max(0.0, static_cast<double>(segment.seconds));
		const auto end{ static_cast<std::size_t>(std::llround(elapsed * sampleRate)) };
		bounds.push_back(end);
		out.starts.push_back(static_cast<float>(static_cast<double>(end) / sampleRate));
	}
	const auto spoken{ bounds.back() };
	const auto total{ spoken + static_cast<std::size_t>(std::max(0.0f, timbre.tail) * rate) };
	out.seconds = static_cast<float>(static_cast<double>(spoken) / sampleRate);
	out.samples.assign(total, 0.0f);

	detail::SfxRng rng{ seed };
	dsp::WhiteNoise breathNoise{ seed * 31 + 7 };
	dsp::WhiteNoise hissNoise{ seed * 57 + 3 };
	std::array<dsp::Svf, 3> formants{};
	dsp::Svf hiss{};
	dsp::OnePole tone{};
	tone.set_cutoff(timbre.brightness, rate);

	const auto glideK{ smoothing_k(timbre.glide, rate) };
	const auto ampK{ smoothing_k(timbre.smoothing, rate) };
	std::array<float, 3> hz{};
	std::array<float, 3> bandwidth{};
	bool voicedYet{};
	float pitch{ 150.0f };
	float voicedAmp{};
	float noiseAmp{};
	float breath{ timbre.breath };
	float phase{};
	float cycleJitter{ 1.0f };
	float cycleShimmer{ 1.0f };
	float previousFlow{};
	float noiseHz{ 4000.0f };
	float noiseQ{ 1.5f };
	std::size_t index{};

	for (std::size_t n{}; n < total; ++n)
	{
		while (index < segments.size() && n >= bounds[index + 1]) ++index;
		float targetVoiced{};
		float targetNoise{};
		float targetBreath{ timbre.breath };
		if (index < segments.size() && n < spoken)
		{
			const auto& segment{ segments[index] };
			const auto length{ std::max<std::size_t>(1, bounds[index + 1] - bounds[index]) };
			const auto t{ static_cast<float>(n - bounds[index]) / static_cast<float>(length) };
			if (segment.kind == VoiceSegmentKind::Voiced)
			{
				targetVoiced = std::max(0.0f, lerp(segment.ampStart, segment.ampEnd, t));
				pitch = std::max(20.0f, lerp(segment.pitchStart, segment.pitchEnd, t));
				targetBreath = std::clamp(timbre.breath + segment.breath, 0.0f, 1.0f);
				for (std::size_t k{}; k < 3; ++k)
				{
					const auto target{ segment.formants[k] };
					if (!voicedYet)
					{
						hz[k] = target.hz;
						bandwidth[k] = target.bandwidth;
					}
					hz[k] += (target.hz - hz[k]) * glideK;
					bandwidth[k] += (target.bandwidth - bandwidth[k]) * glideK;
				}
				voicedYet = true;
			}
			else if (segment.kind == VoiceSegmentKind::Noise)
			{
				targetNoise = std::max(0.0f, lerp(segment.ampStart, segment.ampEnd, t));
				noiseHz = segment.noiseHz;
				noiseQ = segment.noiseQ;
			}
		}
		voicedAmp += (targetVoiced - voicedAmp) * ampK;
		noiseAmp += (targetNoise - noiseAmp) * ampK;
		breath += (targetBreath - breath) * ampK;

		float sample{};
		if (voicedYet)
		{
			const auto vibrato{ 1.0f + timbre.vibratoDepth
				* std::sin(2.0f * std::numbers::pi_v<float> * timbre.vibratoRate * static_cast<float>(n) / rate) };
			const auto f0{ pitch * cycleJitter * vibrato };
			phase += f0 / rate;
			if (phase >= 1.0f)
			{
				phase -= std::floor(phase);
				cycleJitter = 1.0f + timbre.jitter * (rng.unit() * 2.0f - 1.0f);
				cycleShimmer = 1.0f + timbre.shimmer * (rng.unit() * 2.0f - 1.0f);
			}
			const auto flow{ glottal_flow(phase) };
			// The flow's slope, scaled per cycle so it stays the same size at any pitch.
			const auto slope{ (flow - previousFlow) * rate / std::max(f0, 20.0f) * 0.1f };
			previousFlow = flow;
			const auto voiced{ tone.process(slope) };
			const auto aspiration{ breathNoise.next() * (0.35f + 0.65f * flow) * 0.5f };
			const auto excitation{ (voiced * (1.0f - breath) + aspiration * breath) * voicedAmp * cycleShimmer };
			// The resonances move every sample at most a little; resetting their coefficients every 8 is plenty.
			if ((n & 7) == 0)
				for (std::size_t k{}; k < 3; ++k)
					formants[k].set(hz[k], hz[k] / std::max(bandwidth[k], 10.0f), rate);
			for (std::size_t k{}; k < 3; ++k)
			{
				const auto q{ hz[k] / std::max(bandwidth[k], 10.0f) };
				sample += formants[k].process(excitation).band / std::max(q, 0.05f) * s_formant_weights[k] * 4.0f;
			}
		}
		if ((n & 7) == 0) hiss.set(noiseHz, noiseQ, rate);
		sample += hiss.process(hissNoise.next() * noiseAmp).band / std::max(noiseQ, 0.05f) * 0.8f;
		out.samples[n] = soft_clip(sample * timbre.gain);
		out.peak = std::max(out.peak, std::abs(out.samples[n]));
	}
	return out;
}
