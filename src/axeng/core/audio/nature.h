#pragma once

#include "axeng/core/audio/sfx_synth.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace ax::audio
{
	// Generated nature sounds: beds (loops) and one-shots built from noise, filters and simple oscillators, for
	// ambience (docs/audio.md, "Nature sounds"). Each variant's seed makes a different take: a new phrase, other
	// timings.
	enum class NatureKind : std::uint8_t
	{
		swell, // bed: deep sea, brown noise low passed, rising and falling slowly
		surf_wave, // one-shot: a wave swelling, crashing and washing back, with foam
		lapping, // one-shot: a few small slaps of water
		wind, // bed: band passed pink noise wandering, with gusts
		rustle, // one-shot: leaves in a gust
		bird_phrase, // one-shot: a phrase from one of the species
		gull, // one-shot: one to three "kee-ow" calls
		scuttle, // one-shot: a crab's legs clicking
		splash, // one-shot: something breaking the surface, then droplets
		crickets, // bed: a few crickets chirping out of step
		creak, // one-shot: a wooden hull's long stick-slip groan
		rope, // one-shot: a rope's short, tighter creak
		count
	};

	struct NatureParams
	{
		NatureKind kind{ NatureKind::swell };
		// Seconds; 0 takes the kind's own length (a bed's loop length).
		float length{ 0.0f };
		// Scales the sound's frequencies (1 as designed).
		float pitch{ 1.0f };
		// Scales its filters' cutoffs: above 1 brighter.
		float brightness{ 1.0f };
		// How strong: a wave's size, a gust's force.
		float intensity{ 1.0f };
		// Scales how many events it has: chirps, clicks, slaps, grains.
		float density{ 1.0f };
		// Bird species: 0 warbler (trill), 1 whistler, 2 chirper, 3 dove (coo).
		std::uint8_t species{ 0 };
		// Reverb mixed in (0 dry to 1), for distance.
		float reverb{ 0.0f };
		// Peak level of the rendered sound.
		float volume{ 0.8f };
	};

	// The kind's name ("swell", "surf_wave", ...) and back.
	std::string_view nature_kind_name(NatureKind) noexcept;
	std::optional<NatureKind> nature_kind_from_name(std::string_view) noexcept;
	// Whether the kind is a bed (meant to loop).
	bool nature_kind_is_bed(NatureKind) noexcept;
	// The kind's own length in seconds.
	float nature_default_length(NatureKind) noexcept;

	// Clamps every field into its range.
	void clamp_nature(NatureParams&) noexcept;

	// Renders mono float samples at sfx_sample_rate in [-1, 1]. With options.loop, a seamless loop of about the length.
	std::vector<float> render_nature(const NatureParams&, const SynthOptions& options = {});

	// Nudges pitch, brightness, intensity and density by up to about +/- amount of each (like audio::mutate).
	NatureParams mutate_nature(const NatureParams&, float amount, std::uint64_t seed) noexcept;
}
