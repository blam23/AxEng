#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

// A small formant voice: a glottal pulse train with breath noise through three resonances, rendered from a list of
// timed segments. It knows nothing about text; callers turn whatever they like into segments. Deterministic from a seed.
namespace ax::audio
{
	struct VoiceFormant
	{
		float hz{ 500.0f };
		// Bandwidth in Hz (the resonance's width at -3 dB).
		float bandwidth{ 100.0f };
	};

	enum class VoiceSegmentKind : std::uint8_t
	{
		// The glottal source through the formants.
		Voiced,
		// Band-passed noise (hiss) with no glottal source.
		Noise,
		// Silence: the source stops and the filters ring out.
		Gap,
	};

	struct VoiceSegment
	{
		VoiceSegmentKind kind{ VoiceSegmentKind::Gap };
		float seconds{ 0.1f };
		// Fundamental frequency at the segment's start and end (Hz), glided linearly between (voiced only).
		float pitchStart{ 150.0f };
		float pitchEnd{ 150.0f };
		// Loudness at the start and end, glided linearly between (voiced and noise).
		float ampStart{ 1.0f };
		float ampEnd{ 1.0f };
		// The resonances the formants glide toward (voiced only).
		std::array<VoiceFormant, 3> formants{ VoiceFormant{ 500.0f, 90.0f }, VoiceFormant{ 1500.0f, 110.0f },
			VoiceFormant{ 2500.0f, 160.0f } };
		// Breath mixed into the source for this segment, on top of the timbre's (0..1).
		float breath{ 0.0f };
		// The noise band (noise only).
		float noiseHz{ 4000.0f };
		float noiseQ{ 1.5f };
	};

	struct VoiceTimbre
	{
		// Share of aspiration noise in the source (0..1).
		float breath{ 0.2f };
		// Random cycle-to-cycle pitch and loudness wobble, as fractions.
		float jitter{ 0.01f };
		float shimmer{ 0.05f };
		// Low pass on the source (Hz): lower is softer, higher is brighter.
		float brightness{ 3500.0f };
		float vibratoDepth{ 0.0f };
		float vibratoRate{ 5.5f };
		// Seconds for the formants to glide to a new segment's targets.
		float glide{ 0.025f };
		// Seconds for loudness changes to settle (keeps edges click free).
		float smoothing{ 0.006f };
		// Multiplies the scaled source and noise: an amp of 1 lands near this peak.
		float gain{ 0.7f };
		// Seconds rendered after the last segment, for the filters to ring out.
		float tail{ 0.12f };
	};

	struct RenderedVoice
	{
		// Mono samples at the requested rate.
		std::vector<float> samples{};
		// When each segment starts (seconds), and when the last ends.
		std::vector<float> starts{};
		float seconds{};
		float peak{};
	};

	// Renders the segments. Output never exceeds 1: anything louder is soft clipped.
	RenderedVoice render_voice(const VoiceTimbre& timbre, std::span<const VoiceSegment> segments,
		std::uint32_t sampleRate, std::uint64_t seed);
}
