#include "axeng/core/audio/sfx_synth.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace
{
	constexpr std::array<std::string_view, static_cast<std::size_t>(ax::audio::WaveType::Count)> s_wave_names
	{
		"square",
		"saw",
		"sine",
		"noise",
		"triangle",
	};

	using P = ax::audio::SfxParams;
	constexpr std::array<ax::audio::SfxParamInfo, 23> s_param_infos
	{ {
		{ "attack", &P::attack, 0.0f, 1.0f },
		{ "sustain", &P::sustain, 0.0f, 1.0f },
		{ "punch", &P::punch, 0.0f, 1.0f },
		{ "decay", &P::decay, 0.0f, 1.0f },
		{ "freq", &P::baseFreq, 0.0f, 1.0f },
		{ "freq_limit", &P::freqLimit, 0.0f, 1.0f },
		{ "freq_slide", &P::freqSlide, -1.0f, 1.0f },
		{ "freq_delta_slide", &P::freqDeltaSlide, -1.0f, 1.0f },
		{ "vibrato_depth", &P::vibratoDepth, 0.0f, 1.0f },
		{ "vibrato_speed", &P::vibratoSpeed, 0.0f, 1.0f },
		{ "arp_mod", &P::arpMod, -1.0f, 1.0f },
		{ "arp_speed", &P::arpSpeed, 0.0f, 1.0f },
		{ "duty", &P::duty, 0.0f, 1.0f },
		{ "duty_sweep", &P::dutySweep, -1.0f, 1.0f },
		{ "repeat_speed", &P::repeatSpeed, 0.0f, 1.0f },
		{ "phaser_offset", &P::phaserOffset, -1.0f, 1.0f },
		{ "phaser_sweep", &P::phaserSweep, -1.0f, 1.0f },
		{ "lpf_freq", &P::lpfFreq, 0.0f, 1.0f },
		{ "lpf_sweep", &P::lpfSweep, -1.0f, 1.0f },
		{ "lpf_resonance", &P::lpfResonance, 0.0f, 1.0f },
		{ "hpf_freq", &P::hpfFreq, 0.0f, 1.0f },
		{ "hpf_sweep", &P::hpfSweep, -1.0f, 1.0f },
		{ "volume", &P::volume, 0.0f, 1.0f },
	} };

	// Small deterministic RNG so generated sounds are identical across runs and standard library versions.
	class Rng
	{
	public:
		explicit Rng(std::uint64_t seed) noexcept
			: m_state{ seed ^ 0x9E3779B97F4A7C15ull }
		{
			next();
		}

		std::uint64_t next() noexcept
		{
			// splitmix64
			auto z{ (m_state += 0x9E3779B97F4A7C15ull) };
			z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
			z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
			return z ^ (z >> 31);
		}

		float unit() noexcept { return static_cast<float>(next() >> 40) / static_cast<float>(1ull << 24); }
		float range(float lo, float hi) noexcept { return lo + (hi - lo) * unit(); }
		bool chance(float p) noexcept { return unit() < p; }
		std::uint32_t pick(std::uint32_t count) noexcept { return static_cast<std::uint32_t>(next() % count); }

	private:
		std::uint64_t m_state;
	};

	float cube(float v) noexcept { return v * v * v; }

	// Leaves headroom for envelope punch and filter resonance, which can push the raw signal well above 1.
	constexpr float s_output_gain{ 0.5f };

	class Synth
	{
	public:
		Synth(const ax::audio::SfxParams& params, std::uint64_t seed, bool loop) noexcept
			: m_p{ params }
			, m_rng{ seed }
			, m_loop{ loop }
		{
			reset(false);
		}

		int repeat_limit() const noexcept { return m_repeatLimit; }
		int envelope_length(int stage) const noexcept { return m_envLength[stage]; }

		// Returns false once the sound has finished.
		bool next(float& out) noexcept
		{
			if (m_repeatLimit != 0 && ++m_repeatTime >= m_repeatLimit)
			{
				m_repeatTime = 0;
				reset(true);
			}

			if (m_arpLimit != 0 && ++m_arpTime >= m_arpLimit)
			{
				m_arpLimit = 0;
				m_period *= m_arpMul;
			}

			m_slide += m_deltaSlide;
			m_period *= m_slide;
			if (m_period > m_maxPeriod)
			{
				m_period = m_maxPeriod;
				if (m_p.freqLimit > 0.0f && !m_loop)
					return false;
			}

			auto period{ m_period };
			if (m_vibAmp > 0.0)
			{
				m_vibPhase += m_vibSpeed;
				period = m_period * (1.0 + std::sin(m_vibPhase) * m_vibAmp);
			}
			const int iperiod{ std::max(8, static_cast<int>(period)) };

			m_dutyCycle = std::clamp(m_dutyCycle + m_dutySlide, 0.0f, 0.5f);

			float env{ 1.0f };
			if (!m_loop)
			{
				while (m_envStage < 3 && m_envTime >= m_envLength[m_envStage])
				{
					m_envTime = 0;
					++m_envStage;
				}
				if (m_envStage >= 3)
					return false;

				const auto t{ static_cast<float>(m_envTime) / static_cast<float>(m_envLength[m_envStage]) };
				switch (m_envStage)
				{
				case 0: env = t; break;
				case 1: env = 1.0f + (1.0f - t) * 2.0f * m_p.punch; break;
				default: env = 1.0f - t; break;
				}
				++m_envTime;
			}

			m_phaserPos += m_phaserDelta;
			const int phaserOffset{ std::min(1023, std::abs(static_cast<int>(m_phaserPos))) };

			if (m_hpfDelta != 1.0f)
				m_hpfW = std::clamp(m_hpfW * m_hpfDelta, 0.00001f, 0.1f);

			float acc{ 0.0f };
			for (int sub{}; sub < 8; ++sub)
			{
				if (++m_phase >= iperiod)
				{
					m_phase %= iperiod;
					if (m_p.waveType == ax::audio::WaveType::Noise)
						fill_noise();
				}

				const auto fp{ static_cast<float>(m_phase) / static_cast<float>(iperiod) };
				float sample{};
				switch (m_p.waveType)
				{
				case ax::audio::WaveType::Square: sample = fp < m_dutyCycle ? 0.5f : -0.5f; break;
				case ax::audio::WaveType::Saw: sample = 1.0f - fp * 2.0f; break;
				case ax::audio::WaveType::Sine: sample = std::sin(fp * 2.0f * std::numbers::pi_v<float>); break;
				case ax::audio::WaveType::Noise: sample = m_noise[static_cast<std::size_t>(m_phase * 32 / iperiod)]; break;
				case ax::audio::WaveType::Triangle: sample = fp < 0.5f ? fp * 4.0f - 1.0f : 3.0f - fp * 4.0f; break;
				default: break;
				}

				const auto prev{ m_lpfPos };
				m_lpfW = std::clamp(m_lpfW * m_lpfWDelta, 0.0f, 0.1f);
				if (m_p.lpfFreq < 1.0f)
				{
					m_lpfVel += (sample - m_lpfPos) * m_lpfW;
					m_lpfVel -= m_lpfVel * m_lpfDamp;
				}
				else
				{
					m_lpfPos = sample;
					m_lpfVel = 0.0f;
				}
				m_lpfPos += m_lpfVel;

				m_hpfPos += m_lpfPos - prev;
				m_hpfPos -= m_hpfPos * m_hpfW;
				sample = m_hpfPos;

				m_phaser[static_cast<std::size_t>(m_phaserIndex & 1023)] = sample;
				sample += m_phaser[static_cast<std::size_t>((m_phaserIndex - phaserOffset + 1024) & 1023)];
				m_phaserIndex = (m_phaserIndex + 1) & 1023;

				acc += sample * env;
			}

			out = std::clamp(acc / 8.0f * m_p.volume * s_output_gain, -1.0f, 1.0f);
			return true;
		}

	private:
		void fill_noise() noexcept
		{
			for (auto& n : m_noise)
				n = m_rng.range(-1.0f, 1.0f);
		}

		void reset(bool restart) noexcept
		{
			const auto& p{ m_p };
			if (!restart)
				m_phase = 0;

			m_period = 100.0 / (static_cast<double>(p.baseFreq) * p.baseFreq + 0.001);
			m_maxPeriod = 100.0 / (static_cast<double>(p.freqLimit) * p.freqLimit + 0.001);
			m_slide = 1.0 - static_cast<double>(cube(p.freqSlide)) * 0.01;
			m_deltaSlide = -static_cast<double>(cube(p.freqDeltaSlide)) * 0.000001;
			m_dutyCycle = 0.5f - p.duty * 0.5f;
			m_dutySlide = -p.dutySweep * 0.00005f;
			m_arpMul = p.arpMod >= 0.0f ? 1.0 - p.arpMod * p.arpMod * 0.9 : 1.0 + p.arpMod * p.arpMod * 10.0;
			m_arpTime = 0;
			m_arpLimit = p.arpSpeed >= 1.0f ? 0 : static_cast<int>((1.0f - p.arpSpeed) * (1.0f - p.arpSpeed) * 20000.0f + 32.0f);
			if (p.arpMod == 0.0f)
				m_arpLimit = 0;

			if (restart)
				return;

			m_lpfPos = 0.0f;
			m_lpfVel = 0.0f;
			m_lpfW = cube(p.lpfFreq) * 0.1f;
			m_lpfWDelta = 1.0f + p.lpfSweep * 0.0001f;
			m_lpfDamp = std::min(0.8f, 5.0f / (1.0f + p.lpfResonance * p.lpfResonance * 20.0f) * (0.01f + m_lpfW));
			m_hpfPos = 0.0f;
			m_hpfW = p.hpfFreq * p.hpfFreq * 0.1f;
			m_hpfDelta = 1.0f + p.hpfSweep * 0.0003f;

			m_vibPhase = 0.0;
			m_vibSpeed = static_cast<double>(p.vibratoSpeed) * p.vibratoSpeed * 0.01;
			m_vibAmp = static_cast<double>(p.vibratoDepth) * 0.5;

			const auto stage_length{ [](float v) { return static_cast<int>(v * v * 100000.0f); } };
			m_envStage = 0;
			m_envTime = 0;
			m_envLength = { stage_length(p.attack), stage_length(p.sustain), stage_length(p.decay) };

			m_phaserPos = p.phaserOffset * p.phaserOffset * 1020.0f * (p.phaserOffset < 0.0f ? -1.0f : 1.0f);
			m_phaserDelta = p.phaserSweep * p.phaserSweep * (p.phaserSweep < 0.0f ? -1.0f : 1.0f);
			m_phaserIndex = 0;
			m_phaser.fill(0.0f);

			fill_noise();

			m_repeatTime = 0;
			m_repeatLimit = p.repeatSpeed <= 0.0f ? 0 : static_cast<int>((1.0f - p.repeatSpeed) * (1.0f - p.repeatSpeed) * 20000.0f + 32.0f);
		}

		ax::audio::SfxParams m_p;
		Rng m_rng;
		bool m_loop;

		int m_phase{};
		double m_period{};
		double m_maxPeriod{};
		double m_slide{};
		double m_deltaSlide{};
		float m_dutyCycle{};
		float m_dutySlide{};
		double m_arpMul{};
		int m_arpTime{};
		int m_arpLimit{};

		float m_lpfPos{};
		float m_lpfVel{};
		float m_lpfW{};
		float m_lpfWDelta{};
		float m_lpfDamp{};
		float m_hpfPos{};
		float m_hpfW{};
		float m_hpfDelta{};

		double m_vibPhase{};
		double m_vibSpeed{};
		double m_vibAmp{};

		int m_envStage{};
		int m_envTime{};
		std::array<int, 3> m_envLength{};

		float m_phaserPos{};
		float m_phaserDelta{};
		int m_phaserIndex{};
		std::array<float, 1024> m_phaser{};
		std::array<float, 32> m_noise{};

		int m_repeatTime{};
		int m_repeatLimit{};
	};

	using Generator = ax::audio::SfxParams(*)(Rng&);

	ax::audio::SfxParams make_pickup(Rng& r)
	{
		ax::audio::SfxParams p{};
		p.baseFreq = r.range(0.4f, 0.9f);
		p.sustain = r.range(0.0f, 0.1f);
		p.decay = r.range(0.1f, 0.5f);
		p.punch = r.range(0.3f, 0.6f);
		if (r.chance(0.5f))
		{
			p.arpSpeed = r.range(0.5f, 0.7f);
			p.arpMod = r.range(0.2f, 0.6f);
		}
		return p;
	}

	ax::audio::SfxParams make_laser(Rng& r)
	{
		ax::audio::SfxParams p{};
		p.waveType = static_cast<ax::audio::WaveType>(r.pick(3));
		p.baseFreq = r.range(0.5f, 1.0f);
		p.freqLimit = std::max(0.2f, p.baseFreq - r.range(0.2f, 0.8f));
		p.freqSlide = r.range(-0.35f, -0.15f);
		if (r.chance(0.33f))
		{
			p.baseFreq = r.range(0.3f, 0.9f);
			p.freqLimit = r.range(0.0f, 0.1f);
			p.freqSlide = r.range(-0.65f, -0.35f);
		}
		if (r.chance(0.5f))
		{
			p.duty = r.range(0.0f, 0.5f);
			p.dutySweep = r.range(0.0f, 0.2f);
		}
		else
		{
			p.duty = r.range(0.4f, 0.9f);
			p.dutySweep = r.range(-0.7f, 0.0f);
		}
		p.sustain = r.range(0.1f, 0.3f);
		p.decay = r.range(0.0f, 0.4f);
		if (r.chance(0.5f))
			p.punch = r.range(0.0f, 0.3f);
		if (r.chance(0.33f))
		{
			p.phaserOffset = r.range(0.0f, 0.2f);
			p.phaserSweep = r.range(-0.2f, 0.0f);
		}
		if (r.chance(0.5f))
			p.hpfFreq = r.range(0.0f, 0.3f);
		p.volume = 0.4f;
		return p;
	}

	ax::audio::SfxParams make_explosion(Rng& r)
	{
		ax::audio::SfxParams p{};
		p.waveType = ax::audio::WaveType::Noise;
		if (r.chance(0.5f))
		{
			p.baseFreq = r.range(0.1f, 0.5f);
			p.freqSlide = r.range(-0.1f, 0.3f);
		}
		else
		{
			p.baseFreq = r.range(0.2f, 0.9f);
			p.freqSlide = r.range(-0.4f, -0.2f);
		}
		p.baseFreq *= p.baseFreq;
		if (r.chance(0.2f))
			p.freqSlide = 0.0f;
		if (r.chance(0.33f))
			p.repeatSpeed = r.range(0.3f, 0.8f);
		p.sustain = r.range(0.1f, 0.4f);
		p.decay = r.range(0.0f, 0.5f);
		if (r.chance(0.5f))
		{
			p.phaserOffset = r.range(-0.3f, 0.6f);
			p.phaserSweep = r.range(-0.3f, 0.0f);
		}
		p.punch = r.range(0.2f, 0.8f);
		if (r.chance(0.5f))
		{
			p.vibratoDepth = r.range(0.0f, 0.7f);
			p.vibratoSpeed = r.range(0.0f, 0.6f);
		}
		if (r.chance(0.33f))
		{
			p.arpSpeed = r.range(0.6f, 0.9f);
			p.arpMod = r.range(-0.8f, 0.8f);
		}
		return p;
	}

	ax::audio::SfxParams make_powerup(Rng& r)
	{
		ax::audio::SfxParams p{};
		if (r.chance(0.5f))
			p.waveType = ax::audio::WaveType::Saw;
		else
			p.duty = r.range(0.0f, 0.6f);
		p.baseFreq = r.range(0.2f, 0.5f);
		if (r.chance(0.5f))
		{
			p.freqSlide = r.range(0.1f, 0.5f);
			p.repeatSpeed = r.range(0.4f, 0.8f);
		}
		else
		{
			p.freqSlide = r.range(0.05f, 0.25f);
			if (r.chance(0.5f))
			{
				p.vibratoDepth = r.range(0.0f, 0.7f);
				p.vibratoSpeed = r.range(0.0f, 0.6f);
			}
		}
		p.sustain = r.range(0.0f, 0.4f);
		p.decay = r.range(0.1f, 0.5f);
		p.volume = 0.4f;
		return p;
	}

	ax::audio::SfxParams make_hit(Rng& r)
	{
		ax::audio::SfxParams p{};
		constexpr std::array waves{ ax::audio::WaveType::Square, ax::audio::WaveType::Saw, ax::audio::WaveType::Noise };
		p.waveType = waves[r.pick(3)];
		if (p.waveType == ax::audio::WaveType::Square)
			p.duty = r.range(0.0f, 0.6f);
		p.baseFreq = r.range(0.2f, 0.8f);
		p.freqSlide = r.range(-0.7f, -0.3f);
		p.sustain = r.range(0.0f, 0.1f);
		p.decay = r.range(0.1f, 0.3f);
		if (r.chance(0.5f))
			p.hpfFreq = r.range(0.0f, 0.3f);
		return p;
	}

	ax::audio::SfxParams make_jump(Rng& r)
	{
		ax::audio::SfxParams p{};
		p.duty = r.range(0.0f, 0.6f);
		p.baseFreq = r.range(0.3f, 0.6f);
		p.freqSlide = r.range(0.1f, 0.3f);
		p.sustain = r.range(0.1f, 0.4f);
		p.decay = r.range(0.1f, 0.3f);
		if (r.chance(0.5f))
			p.hpfFreq = r.range(0.0f, 0.3f);
		if (r.chance(0.5f))
			p.lpfFreq = r.range(0.4f, 1.0f);
		p.volume = 0.4f;
		return p;
	}

	ax::audio::SfxParams make_blip(Rng& r)
	{
		ax::audio::SfxParams p{};
		p.waveType = r.chance(0.5f) ? ax::audio::WaveType::Square : ax::audio::WaveType::Saw;
		if (p.waveType == ax::audio::WaveType::Square)
			p.duty = r.range(0.0f, 0.6f);
		p.baseFreq = r.range(0.2f, 0.6f);
		p.sustain = r.range(0.1f, 0.2f);
		p.decay = r.range(0.0f, 0.2f);
		p.hpfFreq = 0.1f;
		p.volume = 0.4f;
		return p;
	}

	ax::audio::SfxParams make_click(Rng& r)
	{
		ax::audio::SfxParams p{};
		p.waveType = r.chance(0.5f) ? ax::audio::WaveType::Sine : ax::audio::WaveType::Square;
		p.duty = r.range(0.2f, 0.5f);
		p.baseFreq = r.range(0.55f, 0.75f);
		p.freqSlide = r.range(-0.25f, -0.1f);
		p.sustain = r.range(0.0f, 0.03f);
		p.decay = r.range(0.06f, 0.12f);
		p.punch = r.range(0.3f, 0.5f);
		p.hpfFreq = r.range(0.1f, 0.25f);
		p.volume = 0.45f;
		return p;
	}

	ax::audio::SfxParams make_confirm(Rng& r)
	{
		ax::audio::SfxParams p{};
		p.duty = r.range(0.2f, 0.5f);
		p.baseFreq = r.range(0.38f, 0.48f);
		p.arpMod = r.range(0.25f, 0.4f);
		p.arpSpeed = r.range(0.55f, 0.65f);
		p.sustain = r.range(0.15f, 0.22f);
		p.decay = r.range(0.2f, 0.3f);
		p.punch = r.range(0.1f, 0.25f);
		p.lpfFreq = r.range(0.7f, 0.9f);
		p.volume = 0.35f;
		return p;
	}

	ax::audio::SfxParams make_deny(Rng& r)
	{
		ax::audio::SfxParams p{};
		p.waveType = r.chance(0.5f) ? ax::audio::WaveType::Saw : ax::audio::WaveType::Square;
		p.duty = r.range(0.3f, 0.6f);
		p.baseFreq = r.range(0.22f, 0.3f);
		p.arpMod = r.range(-0.3f, -0.2f);
		p.arpSpeed = r.range(0.55f, 0.65f);
		p.sustain = r.range(0.18f, 0.24f);
		p.decay = r.range(0.1f, 0.2f);
		p.vibratoDepth = r.range(0.1f, 0.2f);
		p.vibratoSpeed = r.range(0.45f, 0.55f);
		p.lpfFreq = r.range(0.45f, 0.6f);
		p.volume = 0.4f;
		return p;
	}

	ax::audio::SfxParams make_hum(Rng& r)
	{
		ax::audio::SfxParams p{};
		p.waveType = r.chance(0.5f) ? ax::audio::WaveType::Square : ax::audio::WaveType::Saw;
		p.duty = r.range(0.2f, 0.5f);
		p.baseFreq = r.range(0.15f, 0.22f);
		p.vibratoDepth = r.range(0.02f, 0.06f);
		p.vibratoSpeed = r.range(0.3f, 0.5f);
		p.lpfFreq = r.range(0.25f, 0.4f);
		p.lpfResonance = r.range(0.2f, 0.5f);
		p.sustain = r.range(0.5f, 0.6f);
		p.decay = 0.2f;
		p.volume = 0.4f;
		return p;
	}

	ax::audio::SfxParams make_hiss(Rng& r)
	{
		ax::audio::SfxParams p{};
		p.waveType = ax::audio::WaveType::Noise;
		p.baseFreq = r.range(0.6f, 0.8f);
		p.hpfFreq = r.range(0.25f, 0.4f);
		p.lpfFreq = r.range(0.55f, 0.75f);
		p.vibratoDepth = r.range(0.05f, 0.15f);
		p.vibratoSpeed = r.range(0.2f, 0.35f);
		p.sustain = r.range(0.6f, 0.7f);
		p.decay = 0.3f;
		p.volume = 0.35f;
		return p;
	}

	// Low-rate noise (crunchy rumble rather than hiss) whose pitch drops and restarts ~9 times a second,
	// with a wobble on top, so it chugs like teeth biting into rock.
	ax::audio::SfxParams make_grind(Rng& r)
	{
		ax::audio::SfxParams p{};
		p.waveType = ax::audio::WaveType::Noise;
		p.baseFreq = r.range(0.12f, 0.16f);
		p.freqSlide = r.range(-0.35f, -0.25f);
		p.repeatSpeed = r.range(0.48f, 0.55f);
		p.vibratoDepth = r.range(0.4f, 0.5f);
		p.vibratoSpeed = r.range(0.55f, 0.65f);
		p.lpfFreq = r.range(0.8f, 0.9f);
		p.lpfResonance = r.range(0.2f, 0.4f);
		p.hpfFreq = r.range(0.12f, 0.18f);
		p.sustain = r.range(0.5f, 0.55f);
		p.decay = 0.2f;
		p.volume = 0.4f;
		return p;
	}

	ax::audio::SfxParams make_whirr(Rng& r)
	{
		ax::audio::SfxParams p{};
		p.waveType = r.chance(0.5f) ? ax::audio::WaveType::Saw : ax::audio::WaveType::Square;
		p.duty = r.range(0.2f, 0.5f);
		p.baseFreq = r.range(0.28f, 0.38f);
		p.freqSlide = r.range(0.05f, 0.12f);
		p.vibratoDepth = r.range(0.1f, 0.2f);
		p.vibratoSpeed = r.range(0.6f, 0.75f);
		p.lpfFreq = r.range(0.35f, 0.5f);
		p.hpfFreq = 0.05f;
		p.attack = r.range(0.05f, 0.1f);
		p.sustain = r.range(0.12f, 0.2f);
		p.decay = r.range(0.1f, 0.18f);
		p.volume = 0.35f;
		return p;
	}

	ax::audio::SfxParams make_thud(Rng& r)
	{
		ax::audio::SfxParams p{};
		p.waveType = ax::audio::WaveType::Noise;
		p.baseFreq = r.range(0.08f, 0.16f);
		p.freqSlide = r.range(-0.2f, -0.1f);
		p.punch = r.range(0.4f, 0.6f);
		p.sustain = r.range(0.03f, 0.08f);
		p.decay = r.range(0.15f, 0.3f);
		p.lpfFreq = r.range(0.25f, 0.4f);
		p.volume = 0.6f;
		return p;
	}

	ax::audio::SfxParams make_spin_up(Rng& r)
	{
		ax::audio::SfxParams p{};
		p.waveType = ax::audio::WaveType::Saw;
		p.baseFreq = r.range(0.1f, 0.16f);
		p.freqSlide = r.range(0.15f, 0.22f);
		p.vibratoDepth = r.range(0.0f, 0.05f);
		p.vibratoSpeed = 0.5f;
		p.lpfFreq = r.range(0.35f, 0.5f);
		p.attack = r.range(0.15f, 0.25f);
		p.sustain = r.range(0.3f, 0.4f);
		p.decay = r.range(0.2f, 0.3f);
		p.volume = 0.35f;
		return p;
	}

	ax::audio::SfxParams make_spin_down(Rng& r)
	{
		ax::audio::SfxParams p{};
		p.waveType = ax::audio::WaveType::Saw;
		p.baseFreq = r.range(0.3f, 0.38f);
		p.freqSlide = r.range(-0.22f, -0.15f);
		p.lpfFreq = r.range(0.35f, 0.5f);
		p.sustain = r.range(0.3f, 0.4f);
		p.decay = r.range(0.3f, 0.4f);
		p.volume = 0.35f;
		return p;
	}

	ax::audio::SfxParams make_random(Rng& r)
	{
		ax::audio::SfxParams p{};
		p.waveType = static_cast<ax::audio::WaveType>(r.pick(static_cast<std::uint32_t>(ax::audio::WaveType::Count)));
		p.baseFreq = std::pow(r.range(-1.0f, 1.0f), 2.0f);
		if (r.chance(0.5f))
			p.baseFreq = cube(r.range(-1.0f, 1.0f)) + 0.5f;
		p.freqSlide = std::pow(r.range(-1.0f, 1.0f), 5.0f);
		if (p.baseFreq > 0.7f && p.freqSlide > 0.2f)
			p.freqSlide = -p.freqSlide;
		if (p.baseFreq < 0.2f && p.freqSlide < -0.05f)
			p.freqSlide = -p.freqSlide;
		p.freqDeltaSlide = cube(r.range(-1.0f, 1.0f));
		p.duty = r.range(-1.0f, 1.0f);
		p.dutySweep = cube(r.range(-1.0f, 1.0f));
		p.vibratoDepth = cube(r.range(-1.0f, 1.0f));
		p.vibratoSpeed = r.range(-1.0f, 1.0f);
		p.attack = cube(r.range(-1.0f, 1.0f));
		p.sustain = r.range(-1.0f, 1.0f) * r.range(-1.0f, 1.0f);
		p.decay = r.range(-1.0f, 1.0f);
		p.punch = std::pow(r.range(0.0f, 0.8f), 2.0f);
		if (p.attack + p.sustain + p.decay < 0.2f)
		{
			p.sustain += r.range(0.2f, 0.5f);
			p.decay += r.range(0.2f, 0.5f);
		}
		p.lpfResonance = r.range(-1.0f, 1.0f);
		p.lpfFreq = 1.0f - cube(r.unit());
		p.lpfSweep = cube(r.range(-1.0f, 1.0f));
		if (p.lpfFreq < 0.1f && p.lpfSweep < -0.05f)
			p.lpfSweep = -p.lpfSweep;
		p.hpfFreq = std::pow(r.unit(), 5.0f);
		p.hpfSweep = std::pow(r.range(-1.0f, 1.0f), 5.0f);
		p.phaserOffset = cube(r.range(-1.0f, 1.0f));
		p.phaserSweep = cube(r.range(-1.0f, 1.0f));
		p.repeatSpeed = r.range(-1.0f, 1.0f);
		p.arpSpeed = r.range(-1.0f, 1.0f);
		p.arpMod = r.range(-1.0f, 1.0f);
		p.volume = 0.35f;
		ax::audio::clamp_params(p);
		return p;
	}

	struct Preset
	{
		std::string_view name;
		Generator generate;
	};

	constexpr std::array<Preset, 18> s_presets
	{ {
		{ "pickup", &make_pickup },
		{ "laser", &make_laser },
		{ "explosion", &make_explosion },
		{ "powerup", &make_powerup },
		{ "hit", &make_hit },
		{ "jump", &make_jump },
		{ "blip", &make_blip },
		{ "click", &make_click },
		{ "confirm", &make_confirm },
		{ "deny", &make_deny },
		{ "hum", &make_hum },
		{ "hiss", &make_hiss },
		{ "grind", &make_grind },
		{ "whirr", &make_whirr },
		{ "thud", &make_thud },
		{ "spin_up", &make_spin_up },
		{ "spin_down", &make_spin_down },
		{ "random", &make_random },
	} };

	constexpr auto s_preset_names{ []
	{
		std::array<std::string_view, s_presets.size()> names{};
		for (std::size_t i{}; i < s_presets.size(); ++i)
			names[i] = s_presets[i].name;
		return names;
	}() };
}

std::string_view ax::audio::wave_type_name(WaveType wave) noexcept
{
	const auto index{ static_cast<std::size_t>(wave) };
	return index < s_wave_names.size() ? s_wave_names[index] : std::string_view{};
}

std::optional<ax::audio::WaveType> ax::audio::wave_type_from_name(std::string_view name) noexcept
{
	for (std::size_t i{}; i < s_wave_names.size(); ++i)
	{
		if (s_wave_names[i] == name)
			return static_cast<WaveType>(i);
	}
	if (name == "sawtooth")
		return WaveType::Saw;
	return std::nullopt;
}

std::span<const ax::audio::SfxParamInfo> ax::audio::sfx_param_infos() noexcept
{
	return s_param_infos;
}

void ax::audio::clamp_params(SfxParams& params) noexcept
{
	for (const auto& info : s_param_infos)
	{
		auto& value{ params.*info.member };
		value = std::isfinite(value) ? std::clamp(value, info.min, info.max) : 0.0f;
	}
	if (static_cast<std::size_t>(params.waveType) >= static_cast<std::size_t>(WaveType::Count))
		params.waveType = WaveType::Square;
}

ax::audio::SfxParams ax::audio::mutate(const SfxParams& params, float amount, std::uint64_t seed) noexcept
{
	auto result{ params };
	if (!(amount > 0.0f))
		return result;

	Rng rng{ seed };
	for (const auto& info : s_param_infos)
	{
		// Volume changes are handled by playback volume variation instead.
		if (info.member == &SfxParams::volume)
			continue;
		const auto delta{ rng.range(-amount, amount) };
		if (rng.chance(0.5f))
			result.*info.member += delta;
	}
	clamp_params(result);
	return result;
}

std::span<const std::string_view> ax::audio::preset_names() noexcept
{
	return s_preset_names;
}

std::optional<ax::audio::SfxParams> ax::audio::generate_preset(std::string_view preset, std::uint64_t seed) noexcept
{
	for (const auto& entry : s_presets)
	{
		if (entry.name == preset)
		{
			Rng rng{ seed };
			auto params{ entry.generate(rng) };
			clamp_params(params);
			return params;
		}
	}
	return std::nullopt;
}

std::vector<float> ax::audio::synthesize(const SfxParams& input, const SynthOptions& options)
{
	auto params{ input };
	clamp_params(params);

	Synth synth{ params, options.seed, options.loop };
	const auto maxSamples{ static_cast<std::size_t>(sfx_max_length * static_cast<float>(sfx_sample_rate)) };

	if (!options.loop)
	{
		std::vector<float> samples{};
		samples.reserve(static_cast<std::size_t>(synth.envelope_length(0) + synth.envelope_length(1) + synth.envelope_length(2)));
		float sample{};
		while (samples.size() < maxSamples && synth.next(sample))
			samples.push_back(sample);

		// Avoid a click if the sound was cut off by the length cap or the frequency limit.
		const auto fade{ std::min<std::size_t>(samples.size(), 64) };
		for (std::size_t i{}; i < fade; ++i)
			samples[samples.size() - 1 - i] *= static_cast<float>(i) / static_cast<float>(fade);
		return samples;
	}

	const auto minLoop{ static_cast<std::size_t>(sfx_sample_rate / 4) };
	auto loopLength{ std::clamp<std::size_t>(static_cast<std::size_t>(synth.envelope_length(1)), minLoop, maxSamples) };
	std::size_t crossfade{};
	if (const auto period{ static_cast<std::size_t>(synth.repeat_limit()) }; period > 0)
	{
		// Keep whole repeat cycles so the rhythm of the loop is even.
		loopLength = std::max<std::size_t>(1, (loopLength + period / 2) / period) * period;
		crossfade = std::min<std::size_t>(64, loopLength / 2);
	}
	else
	{
		crossfade = std::min<std::size_t>(loopLength / 4, sfx_sample_rate * 15 / 100);
	}

	std::vector<float> rendered(loopLength + crossfade);
	for (auto& sample : rendered)
		synth.next(sample);

	// Crossfade the overrun into the start so the end flows seamlessly back to the beginning. Noise is
	// uncorrelated so it uses an equal power fade; everything else fades linearly.
	const bool equalPower{ params.waveType == WaveType::Noise };
	for (std::size_t i{}; i < crossfade; ++i)
	{
		const auto t{ static_cast<float>(i) / static_cast<float>(crossfade) };
		const auto fadeIn{ equalPower ? std::sin(t * std::numbers::pi_v<float> * 0.5f) : t };
		const auto fadeOut{ equalPower ? std::cos(t * std::numbers::pi_v<float> * 0.5f) : 1.0f - t };
		rendered[i] = std::clamp(rendered[i] * fadeIn + rendered[loopLength + i] * fadeOut, -1.0f, 1.0f);
	}
	rendered.resize(loopLength);
	return rendered;
}
