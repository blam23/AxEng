#pragma once

#include <cstdint>

namespace ax::audio::detail
{
	// Small deterministic RNG so generated sounds are identical across runs and standard library versions.
	class SfxRng
	{
	public:
		explicit SfxRng(std::uint64_t seed) noexcept
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
		int between(int lo, int hi) noexcept { return lo + static_cast<int>(pick(static_cast<std::uint32_t>(hi - lo + 1))); }

	private:
		std::uint64_t m_state;
	};
}
