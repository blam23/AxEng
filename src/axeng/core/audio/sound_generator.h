#pragma once

#include "axeng/core/audio/sfx_synth.h"
#include "axeng/core/content_hash.h"

#include <cstdint>
#include <vector>

namespace ax::audio
{
	// A sound defined by the application rather than the engine's own synths (SoundDefinition::generator): it renders
	// each variant itself, and the sound bank plays, caches and loops the results like any other sound.
	class SoundGenerator
	{
	public:
		virtual ~SoundGenerator() = default;
		// Mono float samples at sfx_sample_rate in [-1, 1] for one variant. Variant 0 is the sound as defined; the others
		// may drift from it by up to `mutation` (0 to 1). `seed` differs per variant; options.loop asks for a seamless
		// loop (make_seamless_loop helps), options.seed is the take's own seed.
		virtual std::vector<float> render(std::uint32_t variant, float mutation, std::uint64_t seed,
			const SynthOptions& options) const = 0;
		// Adds everything that changes what render makes, for the render cache's key.
		virtual void hash(ContentHash&) const = 0;
	};
}
