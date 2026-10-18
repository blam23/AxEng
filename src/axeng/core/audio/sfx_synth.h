#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace ax::audio
{
	// Sample rate that generated sound effects are rendered at. The mixer resamples to the device rate.
	inline constexpr std::uint32_t sfx_sample_rate{ 44100 };

	enum class WaveType : std::uint8_t
	{
		Square,
		Saw,
		Sine,
		Noise,
		Triangle,
		Count,
	};

	std::string_view wave_type_name(WaveType wave) noexcept;
	std::optional<WaveType> wave_type_from_name(std::string_view name) noexcept;

	// Parameters for the procedural sound generator. This follows the classic sfxr/bfxr parameter model, so
	// values are normalised (0..1, or -1..1 for signed sweeps) rather than physical units.
	struct SfxParams
	{
		WaveType waveType{ WaveType::Square };

		// Envelope
		float attack{ 0.0f };
		float sustain{ 0.3f };
		float punch{ 0.0f };
		float decay{ 0.4f };

		// Pitch
		float baseFreq{ 0.3f };
		float freqLimit{ 0.0f };
		float freqSlide{ 0.0f };
		float freqDeltaSlide{ 0.0f };
		float vibratoDepth{ 0.0f };
		float vibratoSpeed{ 0.0f };

		// Arpeggio: positive arpMod jumps the pitch up, negative jumps it down.
		float arpMod{ 0.0f };
		float arpSpeed{ 0.0f };

		// Square wave duty cycle
		float duty{ 0.0f };
		float dutySweep{ 0.0f };

		// Restarts the pitch/arpeggio/duty parameters periodically (0 = off).
		float repeatSpeed{ 0.0f };

		float phaserOffset{ 0.0f };
		float phaserSweep{ 0.0f };

		float lpfFreq{ 1.0f };
		float lpfSweep{ 0.0f };
		float lpfResonance{ 0.0f };
		float hpfFreq{ 0.0f };
		float hpfSweep{ 0.0f };

		float volume{ 0.5f };

		bool operator==(const SfxParams&) const = default;
	};

	struct SfxParamInfo
	{
		std::string_view name;
		float SfxParams::* member;
		float min;
		float max;
	};

	// Every float parameter with its Lua name and valid range.
	std::span<const SfxParamInfo> sfx_param_infos() noexcept;

	// Clamps every parameter into its valid range.
	void clamp_params(SfxParams& params) noexcept;

	// Randomly nudges about half of the parameters by up to +/- amount, like bfxr's "mutate".
	// The wave type is never changed.
	SfxParams mutate(const SfxParams& params, float amount, std::uint64_t seed) noexcept;

	// Names of the built in presets, e.g. "pickup", "click", "deny", "hum", "hiss".
	std::span<const std::string_view> preset_names() noexcept;

	// Generates a random sound from the given preset category. The same seed always gives the same sound.
	std::optional<SfxParams> generate_preset(std::string_view preset, std::uint64_t seed) noexcept;

	struct SynthOptions
	{
		// Renders a seamlessly looping buffer: the envelope is held flat for the sustain length and the end is
		// crossfaded into the start.
		bool loop{ false };
		std::uint64_t seed{ 0 };
	};

	// Longest sound the synth will render, in seconds.
	inline constexpr float sfx_max_length{ 8.0f };

	// Renders mono float samples at sfx_sample_rate in the range [-1, 1].
	std::vector<float> synthesize(const SfxParams& params, const SynthOptions& options = {});
}
