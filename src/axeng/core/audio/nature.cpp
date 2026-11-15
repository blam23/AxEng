#include "axeng/core/audio/nature.h"

#include "axeng/core/audio/dsp.h"
#include "axeng/core/audio/sfx_rng.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace
{
	using namespace ax::audio;
	using ax::audio::detail::SfxRng;

	constexpr float s_rate{ static_cast<float>(sfx_sample_rate) };
	constexpr float s_tau{ 2.0f * std::numbers::pi_v<float> };
	// Beds longer than this would cost too much memory per variant.
	constexpr float s_max_length{ 8.0f };

	constexpr std::array<std::string_view, static_cast<std::size_t>(NatureKind::count)> s_names{
		"swell", "surf_wave", "lapping", "wind", "rustle", "bird_phrase", "gull", "scuttle", "splash", "crickets",
		"creak", "rope" };
	constexpr std::array<float, static_cast<std::size_t>(NatureKind::count)> s_lengths{
		6.0f, 6.0f, 1.5f, 6.0f, 2.5f, 2.5f, 1.4f, 0.8f, 1.2f, 6.0f, 1.6f, 0.6f };

	std::size_t frames(float seconds) { return static_cast<std::size_t>(std::max(seconds, 0.01f) * s_rate); }

	// An oscillator whose frequency may change every sample.
	struct Phase
	{
		float phase{};
		float sine(float hz) { phase += s_tau * hz / s_rate; if (phase > s_tau) phase -= s_tau; return std::sin(phase); }
		float saw(float hz) { const auto s{ sine(hz) }; (void)s; return phase / std::numbers::pi_v<float> - 1.0f; }
	};

	// A whole number of cycles over the loop for a rate near hz, so beds wrap without a jump in their modulation.
	float loop_rate(float hz, float length) { return std::max(1.0f, std::round(hz * length)) / length; }

	std::vector<float> swell(const NatureParams& p, float length, std::size_t count, SfxRng& rng)
	{
		std::vector<float> out(count);
		dsp::BrownNoise noise{ rng.next() };
		dsp::Svf low{};
		low.set(180.0f * p.brightness, 0.7f, s_rate);
		const auto rateA{ loop_rate(0.12f, length) }, rateB{ loop_rate(0.07f, length) };
		const auto phaseA{ rng.range(0.0f, s_tau) }, phaseB{ rng.range(0.0f, s_tau) };
		for (std::size_t i{}; i < out.size(); ++i)
		{
			const auto t{ static_cast<float>(i) / s_rate };
			const auto level{ 0.55f + 0.3f * std::sin(s_tau * rateA * t + phaseA) + 0.15f * std::sin(s_tau * rateB * t + phaseB) };
			out[i] = low.process(noise.next()).low * level * p.intensity;
		}
		return out;
	}

	std::vector<float> surf_wave(const NatureParams& p, float length, std::size_t count, SfxRng& rng)
	{
		// Mostly low: a swell rising, the boom of the crash, a roar that sinks as the water runs up and back, and only a
		// little hiss and fizz on top (a wave made of hiss sounds like wind in leaves).
		std::vector<float> out(count);
		dsp::PinkNoise pink{ rng.next() }, hissNoise{ rng.next() };
		dsp::BrownNoise brown{ rng.next() }, boomNoise{ rng.next() };
		dsp::WhiteNoise white{ rng.next() };
		dsp::Svf rumble{}, boom{}, roar{}, hiss{}, foam{};
		rumble.set(240.0f * p.brightness, 0.7f, s_rate);
		boom.set(520.0f * p.brightness, 0.7f, s_rate);
		const auto crash{ rng.range(0.9f, 1.5f) * std::min(1.0f, length / 6.0f) };
		const auto washDecay{ rng.range(1.2f, 2.0f) * std::clamp(p.intensity, 0.4f, 1.6f) };
		dsp::OnePole flutter{};
		flutter.set_cutoff(12.0f, s_rate);
		float flutterTarget{ 1.0f };
		for (std::size_t i{}; i < out.size(); ++i)
		{
			const auto t{ static_cast<float>(i) / s_rate };
			const auto after{ std::max(0.0f, t - crash) };
			const auto rise{ std::clamp(t / crash, 0.0f, 1.0f) };
			const auto swellLevel{ rise * rise * (t < crash ? 1.0f : std::exp(-after / 0.5f)) };
			const auto boomLevel{ dsp::attack_decay(t, crash - 0.25f, 0.3f, 0.7f) };
			const auto roarLevel{ dsp::attack_decay(t, crash - 0.2f, 0.4f, washDecay) };
			const auto hissLevel{ dsp::attack_decay(t, crash, 0.3f, washDecay * 0.6f) };
			if (i % 32 == 0)
			{
				// The roar sinks from about 800 Hz to 250 Hz as the wash runs back; the hiss darkens with it.
				const auto sink{ std::exp(-after / (washDecay * 0.5f)) };
				roar.set((250.0f + 550.0f * sink) * p.brightness, 0.8f, s_rate);
				hiss.set((900.0f + 1100.0f * sink) * p.brightness, 0.6f, s_rate);
				foam.set(3200.0f * p.brightness, 0.6f, s_rate);
			}
			if (i % 2205 == 0) flutterTarget = rng.range(0.5f, 1.0f);
			out[i] = (rumble.process(brown.next()).low * 2.2f * swellLevel
				+ boom.process(boomNoise.next()).low * 3.0f * boomLevel
				+ roar.process(pink.next()).band * 1.4f * roarLevel
				+ hiss.process(hissNoise.next()).low * 0.25f * hissLevel
				+ foam.process(white.next()).band * 0.04f * hissLevel * flutter.process(flutterTarget)) * p.intensity;
		}
		return out;
	}

	std::vector<float> lapping(const NatureParams& p, float length, std::size_t count, SfxRng& rng)
	{
		std::vector<float> out(count);
		const auto slaps{ std::clamp(static_cast<int>(std::round(rng.range(2.0f, 5.0f) * p.density)), 1, 12) };
		for (int slap{}; slap < slaps; ++slap)
		{
			dsp::PinkNoise noise{ rng.next() };
			dsp::Svf band{};
			band.set(rng.range(300.0f, 900.0f) * p.pitch, 4.0f, s_rate);
			const auto start{ rng.range(0.0f, length * 0.7f) };
			const auto decay{ rng.range(0.03f, 0.08f) };
			const auto gain{ rng.range(0.5f, 1.0f) * p.intensity };
			const auto first{ frames(start) };
			const auto last{ std::min(out.size(), first + frames(decay * 8.0f + 0.01f)) };
			for (auto i{ first }; i < last; ++i)
			{
				const auto t{ static_cast<float>(i) / s_rate };
				out[i] += band.process(noise.next()).band * dsp::attack_decay(t, start, 0.006f, decay) * gain;
			}
		}
		return out;
	}

	std::vector<float> wind(const NatureParams& p, float length, std::size_t count, SfxRng& rng)
	{
		std::vector<float> out(count);
		dsp::PinkNoise noise{ rng.next() };
		dsp::BrownNoise low{ rng.next() };
		dsp::Svf band{}, rumble{};
		rumble.set(120.0f * p.brightness, 0.7f, s_rate);
		const auto rateA{ loop_rate(0.17f, length) }, rateB{ loop_rate(0.31f, length) }, rateC{ loop_rate(0.09f, length) };
		const auto phaseA{ rng.range(0.0f, s_tau) }, phaseB{ rng.range(0.0f, s_tau) }, phaseC{ rng.range(0.0f, s_tau) };
		for (std::size_t i{}; i < out.size(); ++i)
		{
			const auto t{ static_cast<float>(i) / s_rate };
			const auto wander{ std::sin(s_tau * rateA * t + phaseA) };
			const auto gust{ 0.5f + 0.5f * std::sin(s_tau * rateC * t + phaseC) };
			if (i % 32 == 0)
				band.set(420.0f * p.pitch * p.brightness * (1.0f + 0.55f * wander + 0.3f * gust),
					0.9f + 0.6f * (0.5f + 0.5f * std::sin(s_tau * rateB * t + phaseB)), s_rate);
			const auto level{ (0.35f + 0.65f * gust * gust) * p.intensity };
			out[i] = band.process(noise.next()).band * level + rumble.process(low.next()).low * 0.4f * level;
		}
		return out;
	}

	std::vector<float> rustle(const NatureParams& p, float length, std::size_t count, SfxRng& rng)
	{
		std::vector<float> out(count);
		dsp::WhiteNoise noise{ rng.next() };
		dsp::Svf high{};
		high.set(2600.0f * p.brightness, 0.6f, s_rate);
		struct Grain { float left{}, gain{}; };
		std::array<Grain, 24> grains{};
		const auto peak{ rng.range(0.35f, 0.6f) };
		for (std::size_t i{}; i < out.size(); ++i)
		{
			const auto t{ static_cast<float>(i) / s_rate };
			const auto u{ t / length };
			const auto gust{ u < peak ? u / peak : std::exp(-(u - peak) * 4.0f) };
			if (rng.chance(0.012f * gust * p.density * p.intensity))
				for (auto& grain : grains)
					if (grain.left <= 0.0f) { grain = { rng.range(0.002f, 0.008f) * s_rate, rng.range(0.2f, 1.0f) }; break; }
			float sum{};
			for (auto& grain : grains)
				if (grain.left > 0.0f) { sum += grain.gain; grain.left -= 1.0f; }
			out[i] = high.process(noise.next() * sum).high * (0.3f + gust);
		}
		return out;
	}

	// One sine note with a frequency curve, for birds.
	void add_note(std::vector<float>& out, float start, float length, float gain, auto&& frequency, float vibrato = 0.0f)
	{
		Phase osc{};
		const auto first{ frames(start) };
		const auto last{ std::min(out.size(), first + frames(length)) };
		for (auto i{ first }; i < last; ++i)
		{
			const auto u{ static_cast<float>(i - first) / std::max(1.0f, static_cast<float>(last - first)) };
			const auto env{ std::min({ 1.0f, u * 6.0f, (1.0f - u) * 6.0f }) };
			const auto hz{ frequency(u) * (1.0f + vibrato * std::sin(s_tau * 7.0f * u * length)) };
			out[i] += osc.sine(hz) * env * gain;
		}
	}

	std::vector<float> bird_phrase(const NatureParams& p, float length, std::size_t count, SfxRng& rng)
	{
		std::vector<float> out(count);
		const auto pitch{ p.pitch * rng.range(0.92f, 1.08f) };
		float at{ 0.05f };
		switch (p.species % 4)
		{
		case 0: // warbler: a rapid trill, its pitch arching over the phrase
		{
			const auto notes{ std::clamp(static_cast<int>(rng.range(8.0f, 18.0f) * p.density), 3, 40) };
			const auto top{ rng.range(3200.0f, 4600.0f) * pitch };
			for (int n{}; n < notes && at < length - 0.1f; ++n)
			{
				const auto u{ static_cast<float>(n) / static_cast<float>(notes) };
				const auto base{ top * (0.85f + 0.25f * std::sin(std::numbers::pi_v<float> * u)) };
				const auto note{ rng.range(0.03f, 0.055f) };
				add_note(out, at, note, 0.8f, [base](float v) { return base * (1.15f - 0.3f * v); }, 0.01f);
				at += note + rng.range(0.012f, 0.03f);
			}
			break;
		}
		case 1: // whistler: two or three long whistles gliding between pitches
		{
			const auto notes{ rng.between(2, 3) };
			for (int n{}; n < notes && at < length - 0.2f; ++n)
			{
				const auto from{ rng.range(1900.0f, 2600.0f) * pitch }, to{ rng.range(2200.0f, 3100.0f) * pitch };
				const auto note{ rng.range(0.22f, 0.4f) };
				add_note(out, at, note, 0.9f, [from, to](float v) { return from + (to - from) * v; }, 0.012f);
				at += note + rng.range(0.08f, 0.2f);
			}
			break;
		}
		case 2: // chirper: short chips falling fast
		{
			const auto notes{ std::clamp(static_cast<int>(rng.range(2.0f, 5.0f) * p.density), 1, 12) };
			for (int n{}; n < notes && at < length - 0.05f; ++n)
			{
				const auto from{ rng.range(5200.0f, 6400.0f) * pitch }, to{ rng.range(2800.0f, 3400.0f) * pitch };
				const auto note{ rng.range(0.02f, 0.04f) };
				add_note(out, at, note, 1.0f, [from, to](float v) { return from + (to - from) * v; });
				at += note + rng.range(0.08f, 0.15f);
			}
			break;
		}
		default: // dove: low soft coos, hoo-HOO-hoo
		{
			const auto notes{ rng.between(3, 5) };
			const auto base{ rng.range(380.0f, 520.0f) * pitch };
			for (int n{}; n < notes && at < length - 0.3f; ++n)
			{
				const auto note{ rng.range(0.28f, 0.45f) };
				const auto lift{ n == 1 ? 1.12f : 1.0f };
				add_note(out, at, note, n == 1 ? 1.0f : 0.7f, [base, lift](float v) { return base * lift * (1.04f - 0.08f * v); });
				// A little body: the second harmonic.
				add_note(out, at, note, 0.15f, [base, lift](float v) { return 2.0f * base * lift * (1.04f - 0.08f * v); });
				at += note + rng.range(0.1f, 0.25f);
			}
			break;
		}
		}
		return out;
	}

	std::vector<float> gull(const NatureParams& p, float length, std::size_t count, SfxRng& rng)
	{
		std::vector<float> out(count);
		dsp::WhiteNoise breath{ rng.next() };
		const auto calls{ rng.between(1, 3) };
		float at{ 0.02f };
		for (int c{}; c < calls && at < length - 0.2f; ++c)
		{
			Phase osc{};
			dsp::Svf formantA{}, formantB{};
			formantA.set(1500.0f * p.brightness, 4.0f, s_rate);
			formantB.set(2800.0f * p.brightness, 5.0f, s_rate);
			const auto call{ rng.range(0.25f, 0.42f) };
			const auto top{ rng.range(1150.0f, 1400.0f) * p.pitch }, bottom{ rng.range(600.0f, 760.0f) * p.pitch };
			const auto first{ frames(at) };
			const auto last{ std::min(out.size(), first + frames(call)) };
			for (auto i{ first }; i < last; ++i)
			{
				const auto u{ static_cast<float>(i - first) / static_cast<float>(last - first) };
				// "kee" rises briefly, "ow" falls away.
				const auto hz{ u < 0.15f ? bottom + (top - bottom) * (u / 0.15f) : top + (bottom - top) * std::pow((u - 0.15f) / 0.85f, 0.7f) };
				const auto env{ std::min({ 1.0f, u * 20.0f, (1.0f - u) * 4.0f }) };
				const auto source{ osc.saw(hz) + breath.next() * 0.15f };
				out[i] += (formantA.process(source).band + 0.7f * formantB.process(source).band) * env;
			}
			at += call + rng.range(0.05f, 0.2f);
		}
		return out;
	}

	std::vector<float> scuttle(const NatureParams& p, float length, std::size_t count, SfxRng& rng)
	{
		std::vector<float> out(count);
		dsp::WhiteNoise noise{ rng.next() };
		dsp::Svf high{};
		high.set(2200.0f * p.brightness, 0.7f, s_rate);
		const auto clicks{ std::clamp(static_cast<int>(rng.range(10.0f, 28.0f) * p.density), 2, 80) };
		float at{ 0.01f };
		for (int c{}; c < clicks && at < length - 0.02f; ++c)
		{
			Phase ping{};
			const auto pingHz{ rng.range(2000.0f, 4200.0f) * p.pitch };
			const auto gain{ rng.range(0.4f, 1.0f) };
			const auto click{ rng.range(0.003f, 0.008f) };
			const auto first{ frames(at) };
			const auto last{ std::min(out.size(), first + frames(click * 3.0f)) };
			for (auto i{ first }; i < last; ++i)
			{
				const auto u{ static_cast<float>(i - first) / s_rate };
				// Hard little taps: the ring carries it, with only a touch of noise (more would sound like leaves).
				const auto env{ std::exp(-u / (click * 0.25f)) };
				out[i] += (high.process(noise.next()).high * 0.25f + ping.sine(pingHz) * 0.9f) * env * gain;
			}
			// Legs in quick runs of three to five, then a pause.
			at += (c % rng.between(3, 5) == 0) ? rng.range(0.05f, 0.1f) : rng.range(0.016f, 0.035f);
		}
		return out;
	}

	std::vector<float> splash(const NatureParams& p, float length, std::size_t count, SfxRng& rng)
	{
		// A bright spray thrown up, a small "plop" under it, a few smaller sprays as it falls back, then droplets: no low
		// thud (that is a drum).
		std::vector<float> out(count);
		dsp::WhiteNoise spray{ rng.next() };
		dsp::PinkNoise plop{ rng.next() };
		dsp::Svf high{}, body{};
		high.set(2200.0f * p.brightness, 0.6f, s_rate);
		body.set(950.0f * p.pitch, 1.5f, s_rate);
		struct Burst { float start, gain, decay; };
		std::vector<Burst> bursts{ { 0.0f, 1.0f, rng.range(0.07f, 0.11f) } };
		for (int b{}, n{ rng.between(2, 4) }; b < n; ++b)
			bursts.push_back({ rng.range(0.05f, 0.35f), rng.range(0.2f, 0.5f), rng.range(0.03f, 0.06f) });
		for (std::size_t i{}; i < out.size(); ++i)
		{
			const auto t{ static_cast<float>(i) / s_rate };
			float level{};
			for (const auto& burst : bursts) level += dsp::attack_decay(t, burst.start, 0.004f, burst.decay) * burst.gain;
			out[i] = (high.process(spray.next()).high * level
				+ body.process(plop.next()).band * 1.6f * dsp::attack_decay(t, 0.0f, 0.002f, 0.045f)) * p.intensity;
		}
		// Droplets: bubbles whose pitch rises as they close.
		const auto drops{ std::clamp(static_cast<int>(rng.range(6.0f, 14.0f) * p.density), 0, 40) };
		for (int d{}; d < drops; ++d)
		{
			const auto start{ rng.range(0.06f, std::max(0.1f, length - 0.1f)) };
			const auto hz{ rng.range(1800.0f, 4500.0f) * p.pitch };
			const auto drop{ rng.range(0.015f, 0.035f) };
			add_note(out, start, drop, rng.range(0.08f, 0.25f), [hz](float v) { return hz * (1.0f + 0.7f * v); });
		}
		return out;
	}

	std::vector<float> crickets(const NatureParams& p, float length, std::size_t count, SfxRng& rng)
	{
		std::vector<float> out(count);
		const auto singers{ std::clamp(static_cast<int>(std::round(rng.range(3.0f, 5.0f) * p.density)), 1, 12) };
		for (int c{}; c < singers; ++c)
		{
			Phase osc{};
			const auto hz{ 4500.0f * p.pitch * rng.range(0.94f, 1.06f) };
			// A whole number of chirps over the loop.
			const auto period{ length / std::max(1.0f, std::round(length / rng.range(0.55f, 1.0f))) };
			const auto offset{ rng.range(0.0f, period) };
			const auto gain{ rng.range(0.3f, 1.0f) };
			for (std::size_t i{}; i < out.size(); ++i)
			{
				const auto t{ static_cast<float>(i) / s_rate };
				const auto inChirp{ std::fmod(t + offset, period) };
				float env{};
				// Three pulses of 20 ms, 30 ms apart.
				for (int pulse{}; pulse < 3; ++pulse) env += dsp::bump(inChirp, pulse * 0.03f, 0.02f);
				out[i] += osc.sine(hz) * env * gain;
			}
		}
		return out;
	}

	// Stick-slip friction: pulses at a wandering rate ringing a few resonances (a hull's groan, a rope's creak).
	std::vector<float> friction(const NatureParams& p, float length, std::size_t count, SfxRng& rng, float lowHz, float highHz,
		std::array<float, 3> modes, float q, float topHz)
	{
		std::vector<float> out(count);
		dsp::Svf top{};
		top.set(topHz * p.brightness, 0.7f, s_rate);
		std::array<dsp::Svf, 3> resonators{};
		for (std::size_t m{}; m < modes.size(); ++m) resonators[m].set(modes[m] * p.brightness * rng.range(0.92f, 1.08f), q, s_rate);
		const auto from{ rng.range(lowHz, highHz) * p.pitch }, to{ rng.range(lowHz, highHz) * p.pitch };
		float next{};
		for (std::size_t i{}; i < out.size(); ++i)
		{
			const auto t{ static_cast<float>(i) / s_rate };
			const auto u{ t / length };
			const auto env{ dsp::bump(t, 0.0f, length) * p.intensity };
			float impulse{};
			next -= 1.0f;
			if (next <= 0.0f)
			{
				// The rate glides from one pitch to another, jittered: the slip is never quite regular.
				const auto hz{ (from + (to - from) * u) * rng.range(0.85f, 1.15f) };
				next = s_rate / std::max(hz, 1.0f);
				impulse = rng.range(0.6f, 1.0f);
			}
			float sum{};
			for (auto& resonator : resonators) sum += resonator.process(impulse).band;
			out[i] = top.process(sum).low * env;
		}
		return out;
	}
}

std::string_view ax::audio::nature_kind_name(NatureKind kind) noexcept
{
	const auto index{ static_cast<std::size_t>(kind) };
	return index < s_names.size() ? s_names[index] : std::string_view{};
}

std::optional<ax::audio::NatureKind> ax::audio::nature_kind_from_name(std::string_view name) noexcept
{
	for (std::size_t i{}; i < s_names.size(); ++i)
		if (s_names[i] == name) return static_cast<NatureKind>(i);
	return std::nullopt;
}

bool ax::audio::nature_kind_is_bed(NatureKind kind) noexcept
{
	return kind == NatureKind::swell || kind == NatureKind::wind || kind == NatureKind::crickets;
}

float ax::audio::nature_default_length(NatureKind kind) noexcept
{
	const auto index{ static_cast<std::size_t>(kind) };
	return index < s_lengths.size() ? s_lengths[index] : 1.0f;
}

void ax::audio::clamp_nature(NatureParams& p) noexcept
{
	const auto safe{ [](float value, float lo, float hi, float fallback)
	{ return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback; } };
	if (static_cast<std::size_t>(p.kind) >= static_cast<std::size_t>(NatureKind::count)) p.kind = NatureKind::swell;
	p.length = safe(p.length, 0.0f, s_max_length, 0.0f);
	p.pitch = safe(p.pitch, 0.25f, 4.0f, 1.0f);
	p.brightness = safe(p.brightness, 0.25f, 4.0f, 1.0f);
	p.intensity = safe(p.intensity, 0.0f, 2.0f, 1.0f);
	p.density = safe(p.density, 0.1f, 4.0f, 1.0f);
	p.species = static_cast<std::uint8_t>(p.species % 4);
	p.reverb = safe(p.reverb, 0.0f, 1.0f, 0.0f);
	p.volume = safe(p.volume, 0.0f, 1.0f, 0.8f);
}

std::vector<float> ax::audio::render_nature(const NatureParams& input, const SynthOptions& options)
{
	auto p{ input };
	clamp_nature(p);
	SfxRng rng{ options.seed ^ (static_cast<std::uint64_t>(p.kind) << 56) };
	const auto length{ p.length > 0.0f ? p.length : nature_default_length(p.kind) };
	// A loop renders past its length, for the splice to search and crossfade into.
	constexpr std::size_t crossfade{ 4096 }, search{ 1024 };
	const auto rendered{ options.loop ? length + static_cast<float>(crossfade + search * 2) / s_rate : length };
	std::vector<float> out{};
	switch (p.kind)
	{
	case NatureKind::swell: out = swell(p, length, frames(rendered), rng); break;
	case NatureKind::surf_wave: out = surf_wave(p, length, frames(rendered), rng); break;
	case NatureKind::lapping: out = lapping(p, length, frames(rendered), rng); break;
	case NatureKind::wind: out = wind(p, length, frames(rendered), rng); break;
	case NatureKind::rustle: out = rustle(p, length, frames(rendered), rng); break;
	case NatureKind::bird_phrase: out = bird_phrase(p, length, frames(rendered), rng); break;
	case NatureKind::gull: out = gull(p, length, frames(rendered), rng); break;
	case NatureKind::scuttle: out = scuttle(p, length, frames(rendered), rng); break;
	case NatureKind::splash: out = splash(p, length, frames(rendered), rng); break;
	case NatureKind::crickets: out = crickets(p, length, frames(rendered), rng); break;
	case NatureKind::creak: out = friction(p, length, frames(rendered), rng, 55.0f, 110.0f, { 220.0f, 480.0f, 910.0f }, 9.0f, 1600.0f); break;
	case NatureKind::rope: out = friction(p, length, frames(rendered), rng, 140.0f, 280.0f, { 720.0f, 1450.0f, 2600.0f }, 11.0f, 4200.0f); break;
	default: break;
	}
	if (p.reverb > 0.0f)
	{
		dsp::Reverb reverb{ s_rate, 0.55f, 0.45f };
		for (auto& sample : out) sample = sample * (1.0f - p.reverb * 0.5f) + reverb.process(sample) * p.reverb * 3.0f;
	}
	dsp::normalise(out, p.volume);
	if (options.loop && out.size() > frames(length))
		out = make_seamless_loop(out, frames(length), crossfade, search);
	else
	{
		// A one-shot's last few milliseconds fade out, so nothing ends on a click.
		const auto fade{ std::min<std::size_t>(out.size(), 256) };
		for (std::size_t i{}; i < fade; ++i) out[out.size() - 1 - i] *= static_cast<float>(i) / static_cast<float>(fade);
	}
	return out;
}

ax::audio::NatureParams ax::audio::mutate_nature(const NatureParams& params, float amount, std::uint64_t seed) noexcept
{
	auto p{ params };
	if (amount <= 0.0f) return p;
	SfxRng rng{ seed };
	const auto nudge{ [&](float& value, float spread) { value *= 1.0f + rng.range(-1.0f, 1.0f) * amount * spread; } };
	nudge(p.pitch, 0.3f);
	nudge(p.brightness, 0.4f);
	nudge(p.intensity, 0.4f);
	nudge(p.density, 0.5f);
	clamp_nature(p);
	return p;
}
