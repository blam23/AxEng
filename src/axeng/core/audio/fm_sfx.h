#pragma once

#include "axeng/core/audio/fm_voice.h"
#include "axeng/core/audio/sfx_synth.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace ax::audio
{
	// A sound effect played on the FM voice: one note held for `hold` seconds, then released. Chorus and reverb
	// sends belong to arrangements, so sound effects have none.
	struct FmSfxParams
	{
		FmInstrumentDefinition patch{};
		std::uint8_t note{ 60 };
		float velocity{ 1.0f };
		float hold{ 0.2f };
		// Loops repeat every loopLength seconds, rounded to 2^16, 2^17 or 2^18 samples (about 1.5, 3 or 6 s).
		float loopLength{ 3.0f };
	};

	// The loop length in samples for a loopLength in seconds: the nearest power of two from 2^16 to 2^18.
	std::uint32_t fm_sfx_loop_frames(float loopLength) noexcept;

	// Clamps note, velocity, hold and loop length into their valid ranges. The patch is left alone (an invalid patch
	// renders silence).
	void clamp_fm_sfx(FmSfxParams& params) noexcept;

	// Renders mono float samples at sfx_sample_rate in [-1, 1], at most sfx_max_length long. A loop holds the note,
	// skips ahead until the sound has settled, and renders one loop length with frequencies snapped to whole cycles
	// (see FmInstrumentVoice::snap_to_loop), then splices it with make_seamless_loop. Returns no samples for an
	// invalid patch.
	std::vector<float> render_fm_sfx(const FmSfxParams& params, const SynthOptions& options = {});

	// Randomly nudges output levels, fine ratios, detune, envelopes, feedback and hold by up to about +/- amount
	// of each range, like audio::mutate. The algorithm, coarse ratios and fixed/ratio mode never change, and
	// silent operators stay silent. An amount of 0 changes nothing.
	FmSfxParams mutate_fm(const FmSfxParams& params, float amount, std::uint64_t seed) noexcept;

	// Names of the FM presets, e.g. "chime", "clank", "alarm", "hum".
	std::span<const std::string_view> fm_preset_names() noexcept;

	// A random FM sound from the given preset family. The same seed always gives the same sound.
	std::optional<FmSfxParams> generate_fm_preset(std::string_view preset, std::uint64_t seed) noexcept;
}
