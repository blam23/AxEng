#pragma once

#include "axeng/core/audio/sfx_rng.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

// Small DSP building blocks for generated sounds (audio::nature), all deterministic from a seed.
namespace ax::audio::dsp
{
	// White noise in [-1, 1].
	class WhiteNoise
	{
	public:
		explicit WhiteNoise(std::uint64_t seed) noexcept : m_rng{ seed } {}
		float next() noexcept { return m_rng.unit() * 2.0f - 1.0f; }

	private:
		detail::SfxRng m_rng;
	};

	// Pink noise (-3 dB an octave) by Paul Kellet's filter on white noise, scaled to about [-1, 1].
	class PinkNoise
	{
	public:
		explicit PinkNoise(std::uint64_t seed) noexcept : m_white{ seed } {}
		float next() noexcept
		{
			const auto w{ m_white.next() };
			m_b[0] = 0.99886f * m_b[0] + w * 0.0555179f;
			m_b[1] = 0.99332f * m_b[1] + w * 0.0750759f;
			m_b[2] = 0.96900f * m_b[2] + w * 0.1538520f;
			m_b[3] = 0.86650f * m_b[3] + w * 0.3104856f;
			m_b[4] = 0.55000f * m_b[4] + w * 0.5329522f;
			m_b[5] = -0.7616f * m_b[5] - w * 0.0168980f;
			const auto pink{ m_b[0] + m_b[1] + m_b[2] + m_b[3] + m_b[4] + m_b[5] + m_b[6] + w * 0.5362f };
			m_b[6] = w * 0.115926f;
			return pink * 0.11f;
		}

	private:
		WhiteNoise m_white;
		std::array<float, 7> m_b{};
	};

	// Brown noise (-6 dB an octave): leaky integrated white noise, scaled to about [-1, 1].
	class BrownNoise
	{
	public:
		explicit BrownNoise(std::uint64_t seed) noexcept : m_white{ seed } {}
		float next() noexcept
		{
			m_last = (m_last + 0.02f * m_white.next()) / 1.02f;
			return m_last * 3.5f;
		}

	private:
		WhiteNoise m_white;
		float m_last{};
	};

	// A one-pole low pass smoother: y += (x - y) * k.
	class OnePole
	{
	public:
		void set_cutoff(float hz, float sampleRate) noexcept
		{
			m_k = 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * std::clamp(hz, 0.0f, sampleRate * 0.49f) / sampleRate);
		}
		float process(float x) noexcept { return m_y += (x - m_y) * m_k; }
		void reset(float value = 0.0f) noexcept { m_y = value; }

	private:
		float m_k{ 1.0f };
		float m_y{};
	};

	// A state variable filter (Simper's trapezoidal form): low, band and high pass from one state, stable when its
	// cutoff moves every sample.
	class Svf
	{
	public:
		struct Out
		{
			float low{}, band{}, high{};
		};

		void set(float cutoffHz, float q, float sampleRate) noexcept
		{
			const auto f{ std::clamp(cutoffHz, 10.0f, sampleRate * 0.45f) };
			m_g = std::tan(std::numbers::pi_v<float> * f / sampleRate);
			m_k = 1.0f / std::max(q, 0.05f);
			m_a1 = 1.0f / (1.0f + m_g * (m_g + m_k));
			m_a2 = m_g * m_a1;
			m_a3 = m_g * m_a2;
		}

		Out process(float x) noexcept
		{
			const auto v3{ x - m_ic2 };
			const auto v1{ m_a1 * m_ic1 + m_a2 * v3 };
			const auto v2{ m_ic2 + m_a2 * m_ic1 + m_a3 * v3 };
			m_ic1 = 2.0f * v1 - m_ic1;
			m_ic2 = 2.0f * v2 - m_ic2;
			return { v2, v1, x - m_k * v1 - v2 };
		}

		void reset() noexcept { m_ic1 = m_ic2 = 0.0f; }

	private:
		float m_g{}, m_k{ 1.0f }, m_a1{}, m_a2{}, m_a3{}, m_ic1{}, m_ic2{};
	};

	// A small Freeverb (Schroeder-Moorer): eight damped combs in parallel into four allpasses, mono.
	class Reverb
	{
	public:
		explicit Reverb(float sampleRate, float roomSize = 0.6f, float damping = 0.4f)
		{
			constexpr std::array<int, 8> combs{ 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
			constexpr std::array<int, 4> allpasses{ 556, 441, 341, 225 };
			const auto scale{ sampleRate / 44100.0f };
			for (std::size_t i{}; i < combs.size(); ++i)
				m_combs[i].buffer.assign(static_cast<std::size_t>(std::max(1.0f, combs[i] * scale)), 0.0f);
			for (std::size_t i{}; i < allpasses.size(); ++i)
				m_allpasses[i].buffer.assign(static_cast<std::size_t>(std::max(1.0f, allpasses[i] * scale)), 0.0f);
			m_feedback = 0.7f + 0.28f * std::clamp(roomSize, 0.0f, 1.0f);
			m_damp = std::clamp(damping, 0.0f, 1.0f);
		}

		// The wet signal for one input sample.
		float process(float x) noexcept
		{
			const auto input{ x * 0.015f };
			float out{};
			for (auto& comb : m_combs)
			{
				auto& stored{ comb.buffer[comb.index] };
				comb.filter = stored * (1.0f - m_damp) + comb.filter * m_damp;
				const auto y{ stored };
				stored = input + comb.filter * m_feedback;
				comb.index = (comb.index + 1) % comb.buffer.size();
				out += y;
			}
			for (auto& allpass : m_allpasses)
			{
				auto& stored{ allpass.buffer[allpass.index] };
				const auto y{ stored - out };
				stored = out + stored * 0.5f;
				allpass.index = (allpass.index + 1) % allpass.buffer.size();
				out = y;
			}
			return out;
		}

	private:
		struct Line
		{
			std::vector<float> buffer{};
			std::size_t index{};
			float filter{};
		};
		std::array<Line, 8> m_combs{};
		std::array<Line, 4> m_allpasses{};
		float m_feedback{}, m_damp{};
	};

	// A smooth 0..1..0 bump over [start, start + length]: raised cosine, 0 outside.
	inline float bump(float t, float start, float length) noexcept
	{
		if (length <= 0.0f || t < start || t > start + length) return 0.0f;
		return 0.5f - 0.5f * std::cos(2.0f * std::numbers::pi_v<float> * (t - start) / length);
	}

	// Attack then exponential decay: 0 before start, rising linearly over attack, then e^(-(t - peak) / decay).
	inline float attack_decay(float t, float start, float attack, float decay) noexcept
	{
		if (t < start) return 0.0f;
		const auto u{ t - start };
		if (u < attack) return attack > 0.0f ? u / attack : 1.0f;
		return std::exp(-(u - attack) / std::max(decay, 1.0e-4f));
	}

	// Scales samples so the loudest reaches peak (silence stays silent).
	inline void normalise(std::vector<float>& samples, float peak) noexcept
	{
		float loudest{};
		for (const auto s : samples) loudest = std::max(loudest, std::abs(s));
		if (loudest <= 1.0e-6f) return;
		const auto gain{ peak / loudest };
		for (auto& s : samples) s *= gain;
	}
}
