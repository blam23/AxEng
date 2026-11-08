#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "axeng/core/audio/fm_voice.h"
#include "axeng/core/audio/instrument.h"

namespace ax::audio
{
	struct ReverbSettings
	{
		float wet{};
		float decaySeconds{ 2.5f };
		float damping{ 0.35f };
	};

	struct ArrangementInstrument
	{
		std::variant<SynthInstrumentDefinition, SamplerInstrumentDefinition, FmInstrumentDefinition> definition{};
		const AudioSampleData* sample{};
		float chorusSend{}; // 0..1: 1 is an even blend of the dry and chorused sound
		float reverbSend{ 1.0f }; // 0..1: how much of the sound reaches the reverb
	};

	struct ArrangementNote
	{
		float startBeat{};
		float durationBeats{ 1.0f };
		std::uint32_t instrument{};
		std::uint8_t note{ 60 };
		float velocity{ 1.0f };
		float pan{};
	};

	struct Arrangement
	{
		float tempo{ 60.0f };
		float durationBeats{};
		std::vector<ArrangementInstrument> instruments{};
		std::vector<ArrangementNote> notes{};
		ReverbSettings reverb{};
	};

	struct PreparedArrangementNote
	{
		std::uint64_t startFrame{};
		std::uint64_t noteOffFrame{};
		std::uint32_t instrument{};
		std::uint8_t note{};
		float velocity{};
		float pan{};
	};

	// An arrangement synthesized ahead of time: interleaved stereo frames at sampleRate, reverb tail included.
	// Samples are stored divided by scale so that peaks above full scale survive the 16-bit conversion.
	struct RenderedArrangement
	{
		std::vector<std::int16_t> samples{};
		float scale{ 1.0f };
		std::uint32_t sampleRate{};

		std::uint64_t frame_count() const noexcept { return samples.size() / 2; }
	};

	struct PreparedArrangement
	{
		float tempo{};
		std::uint64_t durationFrames{};
		std::uint32_t sampleRate{};
		std::vector<ArrangementInstrument> instruments{};
		std::vector<PreparedArrangementNote> notes{};
		ReverbSettings reverb{};
		// When set, playback streams this instead of synthesizing the notes live.
		std::shared_ptr<const RenderedArrangement> rendered{};
	};

	constexpr std::size_t arrangement_max_instruments{ 64 };
	constexpr std::size_t arrangement_max_notes{ 16384 };
	constexpr std::size_t arrangement_max_voices{ 64 };
	constexpr float arrangement_max_duration_seconds{ 1800.0f };

	std::shared_ptr<const PreparedArrangement> prepare_arrangement(
		Arrangement arrangement, std::uint32_t sampleRate, std::string& error);
}
