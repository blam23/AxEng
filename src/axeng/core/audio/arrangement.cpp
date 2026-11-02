#include "axeng/core/audio/arrangement.h"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <set>
#include <type_traits>
#include <utility>

namespace
{
	struct PolyphonyChange
	{
		std::uint64_t frame{};
		int delta{};
	};

	std::uint64_t seconds_to_frames(double seconds, std::uint32_t sampleRate) noexcept
	{
		return static_cast<std::uint64_t>(std::llround(seconds * static_cast<double>(sampleRate)));
	}

	float release_seconds(const ax::audio::ArrangementInstrument& instrument) noexcept
	{
		return std::visit([](const auto& definition) { return definition.envelope.release; }, instrument.definition);
	}

	bool valid_instrument(const ax::audio::ArrangementInstrument& instrument, std::uint32_t sampleRate,
		std::set<const ax::audio::AudioSampleData*>& validatedSamples)
	{
		return std::visit([&](const auto& definition)
		{
			using Definition = std::decay_t<decltype(definition)>;
			if constexpr (std::is_same_v<Definition, ax::audio::SynthInstrumentDefinition>)
			{
				return ax::audio::SynthInstrumentVoice{ definition, 60, 1.0f, sampleRate }.valid()
					&& instrument.sample == nullptr;
			}
			else
			{
				if (instrument.sample == nullptr)
					return false;
				if (validatedSamples.insert(instrument.sample).second && !instrument.sample->valid())
					return false;
				return ax::audio::SamplerInstrumentVoice{
					*instrument.sample, definition, 60, 1.0f, sampleRate }.valid();
			}
		}, instrument.definition);
	}
}

std::shared_ptr<const ax::audio::PreparedArrangement> ax::audio::prepare_arrangement(
	Arrangement arrangement, std::uint32_t sampleRate, std::string& error)
{
	error.clear();
	if (sampleRate < 8000 || sampleRate > 192000)
	{
		error = "sample rate is outside the supported range";
		return nullptr;
	}
	if (!std::isfinite(arrangement.tempo) || arrangement.tempo < 20.0f || arrangement.tempo > 300.0f)
	{
		error = "tempo must be between 20 and 300 BPM";
		return nullptr;
	}
	if (!std::isfinite(arrangement.durationBeats) || !(arrangement.durationBeats > 0.0f)
		|| static_cast<double>(arrangement.durationBeats) * 60.0 / arrangement.tempo > arrangement_max_duration_seconds)
	{
		error = "duration must be positive and no longer than 30 minutes";
		return nullptr;
	}
	if (arrangement.instruments.empty() || arrangement.instruments.size() > arrangement_max_instruments)
	{
		error = "arrangement must contain between 1 and 64 instruments";
		return nullptr;
	}
	if (arrangement.notes.size() > arrangement_max_notes)
	{
		error = "arrangement exceeds the 16384-note limit";
		return nullptr;
	}
	if (!std::isfinite(arrangement.reverb.wet) || arrangement.reverb.wet < 0.0f
		|| arrangement.reverb.wet > 1.0f
		|| !std::isfinite(arrangement.reverb.decaySeconds) || arrangement.reverb.decaySeconds < 0.1f
		|| arrangement.reverb.decaySeconds > 8.0f
		|| !std::isfinite(arrangement.reverb.damping) || arrangement.reverb.damping < 0.0f
		|| arrangement.reverb.damping > 0.95f)
	{
		error = "reverb wet must be 0..1, decay 0.1..8 seconds, and damping 0..0.95";
		return nullptr;
	}

	std::set<const AudioSampleData*> validatedSamples{};
	for (std::size_t i{}; i < arrangement.instruments.size(); ++i)
	{
		if (!valid_instrument(arrangement.instruments[i], sampleRate, validatedSamples))
		{
			error = "instrument " + std::to_string(i + 1) + " has an invalid definition or sample";
			return nullptr;
		}
	}

	const auto framesPerBeat{ 60.0 * static_cast<double>(sampleRate) / arrangement.tempo };
	auto prepared{ std::make_shared<PreparedArrangement>() };
	prepared->tempo = arrangement.tempo;
	prepared->sampleRate = sampleRate;
	prepared->reverb = arrangement.reverb;
	prepared->durationFrames = seconds_to_frames(
		static_cast<double>(arrangement.durationBeats) * 60.0 / arrangement.tempo, sampleRate);
	prepared->instruments = std::move(arrangement.instruments);
	prepared->notes.reserve(arrangement.notes.size());

	std::vector<PolyphonyChange> changes{};
	changes.reserve(prepared->notes.capacity() * 2);
	for (const auto& note : arrangement.notes)
	{
		if (!std::isfinite(note.startBeat) || note.startBeat < 0.0f
			|| !std::isfinite(note.durationBeats) || !(note.durationBeats > 0.0f)
			|| note.instrument >= prepared->instruments.size()
			|| !std::isfinite(note.velocity) || !(note.velocity > 0.0f) || note.velocity > 1.0f
			|| !std::isfinite(note.pan) || note.pan < -1.0f || note.pan > 1.0f
			|| static_cast<double>(note.startBeat) + note.durationBeats > arrangement.durationBeats)
		{
			error = "arrangement contains an invalid note event";
			return nullptr;
		}

		const auto startFrame{ seconds_to_frames(static_cast<double>(note.startBeat) * framesPerBeat / sampleRate, sampleRate) };
		const auto noteOffFrame{ seconds_to_frames(
			(static_cast<double>(note.startBeat) + note.durationBeats) * framesPerBeat / sampleRate, sampleRate) };
		if (noteOffFrame <= startFrame)
		{
			error = "note duration is shorter than one audio frame";
			return nullptr;
		}
		const auto releaseFrames{ seconds_to_frames(
			release_seconds(prepared->instruments[note.instrument]), sampleRate) };
		prepared->notes.push_back(PreparedArrangementNote{
			.startFrame = startFrame,
			.noteOffFrame = noteOffFrame,
			.instrument = note.instrument,
			.note = note.note,
			.velocity = note.velocity,
			.pan = note.pan });
		changes.push_back({ .frame = startFrame, .delta = 1 });
		changes.push_back({ .frame = noteOffFrame + releaseFrames, .delta = -1 });
	}

	std::ranges::stable_sort(prepared->notes, {}, &PreparedArrangementNote::startFrame);
	std::ranges::sort(changes, [](const PolyphonyChange& left, const PolyphonyChange& right)
	{
		return left.frame != right.frame ? left.frame < right.frame : left.delta < right.delta;
	});

	std::size_t active{};
	for (const auto& change : changes)
	{
		if (change.delta < 0)
			--active;
		else if (++active > arrangement_max_voices)
		{
			error = "arrangement exceeds the 64-voice polyphony limit";
			return nullptr;
		}
	}
	return prepared;
}
