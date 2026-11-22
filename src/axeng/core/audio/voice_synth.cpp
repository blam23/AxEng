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
	constexpr int s_max_unison{ 6 };

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
	// A smooth voice is a darker one too: the buzz lives in the source's upper harmonics.
	tone.set_cutoff(timbre.brightness * (1.0f - 0.7f * std::clamp(timbre.smooth, 0.0f, 1.0f)), rate);

	const auto glideK{ smoothing_k(timbre.glide, rate) };
	const auto ampK{ smoothing_k(timbre.smoothing, rate) };
	std::array<float, 3> hz{};
	std::array<float, 3> bandwidth{};
	bool voicedYet{};
	float pitch{ 150.0f };
	float voicedAmp{};
	float noiseAmp{};
	float breath{ timbre.breath };
	float cycleJitter{ 1.0f };
	float noiseHz{ 4000.0f };
	float noiseQ{ 1.5f };
	float subPhase{};
	float popPhase{};
	struct UnisonVoice
	{
		float phase{}, jitter{ 1.0f }, shimmer{ 1.0f }, previous{};
	};
	std::array<UnisonVoice, s_max_unison> unison{};
	// Each voice of a unison starts at its own point in the cycle, so they don't all strike together.
	for (std::size_t u{ 1 }; u < unison.size(); ++u) unison[u].phase = rng.unit();
	std::size_t index{};
	const auto twoPi{ 2.0f * std::numbers::pi_v<float> };

	for (std::size_t n{}; n < total; ++n)
	{
		while (index < segments.size() && n >= bounds[index + 1]) ++index;
		float targetVoiced{};
		float targetNoise{};
		float targetBreath{ timbre.breath };
		float pop{};
		// The slow wobble: a sine whose phase itself wanders, so it never settles into a beat.
		const auto seconds{ static_cast<float>(n) / rate };
		const auto wobble{ std::sin(twoPi * timbre.wobbleRate * seconds
			+ 1.3f * std::sin(twoPi * timbre.wobbleRate * 0.37f * seconds)) };
		if (index < segments.size() && n < spoken)
		{
			const auto& segment{ segments[index] };
			const auto length{ std::max<std::size_t>(1, bounds[index + 1] - bounds[index]) };
			const auto t{ static_cast<float>(n - bounds[index]) / static_cast<float>(length) };
			if (segment.kind == VoiceSegmentKind::Pop)
			{
				if (n == bounds[index]) popPhase = 0.0f;
				const auto hz{ lerp(segment.pitchStart, segment.pitchEnd, t) };
				popPhase += hz / rate;
				const auto envelope{ std::min(1.0f, t * 12.0f) * std::exp(-5.0f * t) };
				pop = std::sin(twoPi * popPhase) * envelope * segment.ampStart;
			}
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
			const auto f0{ pitch * cycleJitter * vibrato * (1.0f + timbre.wobblePitch * wobble) };
			// The source: one voice, or several in unison, each a little detuned, drifting and wobbling on its own,
			// so together they beat like a choir. Each sounds the glottal flow's slope (bright, buzzy) blended toward
			// the flow itself (round, soft) by `smooth`.
			const auto voices{ std::clamp(timbre.unison, 1, s_max_unison) };
			const auto smooth{ std::clamp(timbre.smooth, 0.0f, 1.0f) };
			float source{}, flowSum{}, shimmerSum{};
			for (int u{}; u < voices; ++u)
			{
				auto& v{ unison[static_cast<std::size_t>(u)] };
				float detune{ 1.0f };
				if (voices > 1)
				{
					const auto spread{ static_cast<float>(u) / static_cast<float>(voices - 1) * 2.0f - 1.0f };
					const auto sway{ 1.0f + 0.35f * std::sin(twoPi * (0.17f + 0.11f * static_cast<float>(u)) * seconds + static_cast<float>(u)) };
					detune = std::exp2(timbre.detuneCents * spread * sway / 1200.0f);
				}
				const auto fu{ (u == 0 ? f0 : f0 / cycleJitter * v.jitter) * detune };
				v.phase += fu / rate;
				if (v.phase >= 1.0f)
				{
					v.phase -= std::floor(v.phase);
					v.jitter = 1.0f + timbre.jitter * (rng.unit() * 2.0f - 1.0f);
					v.shimmer = 1.0f + timbre.shimmer * (rng.unit() * 2.0f - 1.0f);
					if (u == 0) cycleJitter = v.jitter;
				}
				const auto flow{ glottal_flow(v.phase) };
				// The flow's slope, scaled per cycle so it stays the same size at any pitch.
				const auto slope{ (flow - v.previous) * rate / std::max(fu, 20.0f) * 0.1f };
				v.previous = flow;
				source += (slope * (1.0f - smooth) + flow * smooth * 0.6f) * v.shimmer;
				flowSum += flow;
				shimmerSum += v.shimmer;
			}
			const auto norm{ 1.0f / std::sqrt(static_cast<float>(voices)) };
			const auto voiced{ tone.process(source * norm) };
			const auto flow{ flowSum / static_cast<float>(voices) };
			const auto aspiration{ breathNoise.next() * (0.35f + 0.65f * flow) * 0.5f };
			const auto excitation{ (voiced * (1.0f - breath) + aspiration * breath) * voicedAmp
				* (voices > 1 ? 1.0f : shimmerSum) };
			// The resonances move every sample at most a little; resetting their coefficients every 8 is plenty.
			const auto drift{ 1.0f + timbre.wobbleDepth * wobble };
			if ((n & 7) == 0)
				for (std::size_t k{}; k < 3; ++k)
					formants[k].set(hz[k] * drift, hz[k] * drift / std::max(bandwidth[k], 10.0f), rate);
			for (std::size_t k{}; k < 3; ++k)
			{
				const auto q{ hz[k] * drift / std::max(bandwidth[k], 10.0f) };
				sample += formants[k].process(excitation).band / std::max(q, 0.05f) * s_formant_weights[k] * 4.0f;
			}
			// The hum an octave down, from beside the voice (not through its formants).
			if (timbre.subharmonic > 0.0f)
			{
				subPhase += f0 * 0.5f / rate;
				subPhase -= std::floor(subPhase);
				sample += std::sin(twoPi * subPhase) * timbre.subharmonic * voicedAmp * 0.35f;
			}
			// Ring modulation: the voice swung in loudness at an audible rate, for a metallic buzz.
			if (timbre.ringDepth > 0.0f)
				sample *= 1.0f - timbre.ringDepth + timbre.ringDepth * std::sin(twoPi * timbre.ringHz * seconds);
		}
		sample += pop * 0.5f;
		if ((n & 7) == 0) hiss.set(noiseHz, noiseQ, rate);
		sample += hiss.process(hissNoise.next() * noiseAmp).band / std::max(noiseQ, 0.05f) * 0.8f;
		out.samples[n] = soft_clip(sample * timbre.gain);
		out.peak = std::max(out.peak, std::abs(out.samples[n]));
	}
	// The reverse reverb: run the reverb from the end to the start, so each sound's tail stretches back before it, and
	// mix that in. A lead-in of silence in front gives the first sound room to swell into.
	if (timbre.reverseMix > 0.0f && !out.samples.empty())
	{
		const auto leadFrames{ static_cast<std::size_t>(std::max(0.0f, timbre.reverseLead) * rate) };
		std::vector<float> mixed(leadFrames + out.samples.size(), 0.0f);
		std::ranges::copy(out.samples, mixed.begin() + static_cast<std::ptrdiff_t>(leadFrames));
		dsp::Reverb reverb{ rate, std::clamp(timbre.reverseRoom, 0.0f, 1.0f), 0.45f };
		std::vector<float> wet(mixed.size(), 0.0f);
		for (auto i{ mixed.size() }; i-- > 0;) wet[i] = reverb.process(mixed[i]);
		out.peak = 0.0f;
		for (std::size_t i{}; i < mixed.size(); ++i)
		{
			mixed[i] = soft_clip(mixed[i] + wet[i] * timbre.reverseMix * 2.5f);
			out.peak = std::max(out.peak, std::abs(mixed[i]));
		}
		out.samples = std::move(mixed);
		out.lead = static_cast<float>(static_cast<double>(leadFrames) / sampleRate);
	}
	return out;
}
