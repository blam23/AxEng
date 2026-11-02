#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

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
		std::variant<SynthInstrumentDefinition, SamplerInstrumentDefinition> definition{};
		const AudioSampleData* sample{};
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

	struct PreparedArrangement
	{
		float tempo{};
		std::uint64_t durationFrames{};
		std::uint32_t sampleRate{};
		std::vector<ArrangementInstrument> instruments{};
		std::vector<PreparedArrangementNote> notes{};
		ReverbSettings reverb{};
	};

	constexpr std::size_t arrangement_max_instruments{ 64 };
	constexpr std::size_t arrangement_max_notes{ 16384 };
	constexpr std::size_t arrangement_max_voices{ 64 };
	constexpr float arrangement_max_duration_seconds{ 1800.0f };

	std::shared_ptr<const PreparedArrangement> prepare_arrangement(
		Arrangement arrangement, std::uint32_t sampleRate, std::string& error);
}
