#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ax::audio
{
	enum class InstrumentWave : std::uint8_t
	{
		Sine,
		Triangle,
		Saw,
		Square,
		Noise,
	};

	struct InstrumentEnvelope
	{
		float attack{ 0.01f };
		float decay{ 0.0f };
		float sustain{ 1.0f };
		float release{ 0.1f };
	};

	struct SynthInstrumentDefinition
	{
		InstrumentWave wave{ InstrumentWave::Sine };
		InstrumentEnvelope envelope{};
		float gain{ 0.25f };
		float detuneCents{ 0.0f };
		float vibratoCents{ 0.0f };
		float vibratoRate{ 0.0f };
		float lowPassHz{ 20000.0f };
		float pitchDropSemitones{ 0.0f };
		float pitchDropSeconds{ 0.0f };
	};

	struct AudioSampleData
	{
		std::vector<float> samples{};
		std::uint32_t sampleRate{};
		std::uint32_t channels{};

		std::size_t frame_count() const noexcept;
		bool metadata_valid() const noexcept;
		bool valid() const noexcept;
	};

	std::optional<AudioSampleData> decode_audio_sample(std::span<const std::uint8_t> encoded);
	std::optional<AudioSampleData> render_synth_sample(const SynthInstrumentDefinition& definition,
		std::uint8_t note, float velocity, float holdSeconds, std::uint32_t sampleRate, std::string& error);
	constexpr float synth_sample_max_seconds{ 30.0f };

	struct SamplerInstrumentDefinition
	{
		std::uint8_t rootNote{ 60 };
		float tuningCents{ 0.0f };
		std::uint64_t startFrame{ 0 };
		std::uint64_t endFrame{ 0 };
		std::uint64_t loopStartFrame{ 0 };
		std::uint64_t loopEndFrame{ 0 };
		InstrumentEnvelope envelope{};
		float gain{ 1.0f };
		bool loop{ false };
	};

	class EnvelopeState
	{
	public:
		void start(const InstrumentEnvelope& envelope, float velocity, std::uint32_t sampleRate) noexcept;
		void note_off() noexcept;
		void advance(std::uint64_t frames) noexcept;
		float next() noexcept;
		bool finished() const noexcept { return m_finished; }

	private:
		enum class Stage : std::uint8_t { Attack, Decay, Sustain, Release, Finished };

		InstrumentEnvelope m_envelope{};
		Stage m_stage{ Stage::Finished };
		std::uint64_t m_stagePosition{};
		std::uint64_t m_attackFrames{};
		std::uint64_t m_decayFrames{};
		std::uint64_t m_releaseFrames{};
		std::uint32_t m_sampleRate{};
		float m_velocity{};
		float m_level{};
		float m_releaseStart{};
		bool m_finished{ true };
	};

	class SynthInstrumentVoice
	{
	public:
		SynthInstrumentVoice(const SynthInstrumentDefinition& definition, std::uint8_t note,
			float velocity, std::uint32_t sampleRate) noexcept;

		bool valid() const noexcept { return m_valid; }
		bool finished() const noexcept { return !m_valid || m_envelope.finished(); }
		void note_off() noexcept { m_envelope.note_off(); }
		void advance(std::uint64_t frames) noexcept;
		bool next(float& sample) noexcept;

	private:
		SynthInstrumentDefinition m_definition{};
		EnvelopeState m_envelope{};
		std::uint32_t m_sampleRate{};
		double m_baseFrequency{};
		double m_phase{};
		double m_vibratoPhase{};
		std::uint64_t m_samplePosition{};
		std::uint8_t m_noiseSeed{};
		float m_filterState{};
		float m_filterAlpha{};
		bool m_valid{};
	};

	class SamplerInstrumentVoice
	{
	public:
		SamplerInstrumentVoice(const AudioSampleData& sample, const SamplerInstrumentDefinition& definition,
			std::uint8_t note, float velocity, std::uint32_t outputSampleRate) noexcept;

		bool valid() const noexcept { return m_valid; }
		bool finished() const noexcept { return !m_valid || m_envelope.finished()
			|| (!m_definition.loop && m_position >= static_cast<double>(m_endFrame)); }
		void note_off() noexcept { m_envelope.note_off(); }
		void advance(std::uint64_t frames) noexcept;
		bool next(float& left, float& right) noexcept;

	private:
		const AudioSampleData* m_sample{};
		SamplerInstrumentDefinition m_definition{};
		EnvelopeState m_envelope{};
		double m_position{};
		double m_step{};
		std::uint64_t m_endFrame{};
		std::uint64_t m_loopStartFrame{};
		std::uint64_t m_loopEndFrame{};
		bool m_valid{};
	};
}
