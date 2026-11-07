#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "axeng/core/audio/instrument.h"

namespace ax::audio
{
	constexpr std::size_t fm_operator_count{ 6 };
	constexpr std::uint8_t fm_algorithm_count{ 32 };

	enum class FmScalingCurve : std::uint8_t
	{
		NegativeLinear,
		NegativeExponential,
		PositiveExponential,
		PositiveLinear,
	};

	enum class FmLfoWave : std::uint8_t
	{
		Triangle,
		SawDown,
		SawUp,
		Square,
		Sine,
		SampleHold,
	};

	// Four rate/level stages, each 0..99. A note starts at levels[3], moves through levels[0..2], holds at levels[2]
	// until note off, then moves back to levels[3].
	struct FmEnvelope
	{
		std::array<std::uint8_t, 4> rates{ 99, 99, 99, 99 };
		std::array<std::uint8_t, 4> levels{ 99, 99, 99, 0 };
	};

	struct FmOperator
	{
		FmEnvelope envelope{};
		std::uint8_t outputLevel{ 99 };
		bool fixedFrequency{ false };
		std::uint8_t coarse{ 1 };                  // ratio: 0 is 0.5, otherwise the multiple (0..31); fixed: 1, 10, 100 or 1000 Hz
		std::uint8_t fine{ 0 };                    // 0..99: ratio adds 1% per step, fixed scales by 10^(fine/100)
		std::int8_t detune{ 0 };                   // -7..7
		std::uint8_t velocitySensitivity{ 0 };     // 0..7
		std::uint8_t ampModSensitivity{ 0 };       // 0..3
		std::uint8_t rateScaling{ 0 };             // 0..7: higher notes run the envelope faster
		std::uint8_t levelScalingBreakpoint{ 39 }; // 0..99, MIDI note minus 21
		std::uint8_t levelScalingLeftDepth{ 0 };   // 0..99
		std::uint8_t levelScalingRightDepth{ 0 };  // 0..99
		FmScalingCurve levelScalingLeftCurve{ FmScalingCurve::NegativeLinear };
		FmScalingCurve levelScalingRightCurve{ FmScalingCurve::NegativeLinear };
	};

	struct FmLfo
	{
		FmLfoWave wave{ FmLfoWave::Triangle };
		std::uint8_t speed{ 35 };              // 0..99
		std::uint8_t delay{ 0 };               // 0..99
		std::uint8_t pitchModDepth{ 0 };       // 0..99
		std::uint8_t ampModDepth{ 0 };         // 0..99
		std::uint8_t pitchModSensitivity{ 0 }; // 0..7
	};

	struct FmInstrumentDefinition
	{
		std::array<FmOperator, fm_operator_count> operators{};
		std::uint8_t algorithm{ 1 }; // 1..32
		std::uint8_t feedback{ 0 };  // 0..7
		std::int8_t transpose{ 0 };  // -24..24 semitones
		FmEnvelope pitchEnvelope{ .rates = { 99, 99, 99, 99 }, .levels = { 50, 50, 50, 50 } }; // 50 is no pitch change
		FmLfo lfo{};
		// Gates the whole voice, so its release is the length of the note's tail for scheduling and polyphony. Set the
		// release to cover the slowest carrier's operator release.
		InstrumentEnvelope envelope{ .attack = 0.0f, .decay = 0.0f, .sustain = 1.0f, .release = 0.1f };
		float gain{ 0.25f };
		// Lifts a patch whose carriers peak below full scale, so that gain can still mean its peak level (1..16).
		float outputTrim{ 1.0f };
	};

	// Operator masks use bit i for operator i + 1. Every modulator has a higher number than the operator it modulates.
	struct FmAlgorithm
	{
		std::array<std::uint8_t, fm_operator_count> modulators{};
		std::uint8_t carriers{};
		std::uint8_t feedbackTarget{}; // zero-based operator index
		std::uint8_t feedbackSource{}; // zero-based; differs from the target when feedback spans several operators
	};

	// Returns nullptr unless algorithm is 1..32.
	const FmAlgorithm* fm_algorithm(std::uint8_t algorithm) noexcept;

	std::optional<AudioSampleData> render_fm_sample(const FmInstrumentDefinition& definition, std::uint8_t note,
		float velocity, float holdSeconds, std::uint32_t sampleRate, std::string& error);

	class FmInstrumentVoice
	{
	public:
		FmInstrumentVoice(const FmInstrumentDefinition& definition, std::uint8_t note, float velocity,
			std::uint32_t sampleRate) noexcept;

		bool valid() const noexcept { return m_valid; }
		bool finished() const noexcept { return !m_valid || m_envelope.finished(); }
		void note_off() noexcept;
		void advance(std::uint64_t frames) noexcept;
		bool next(float& sample) noexcept;

	private:
		enum class Stage : std::uint8_t { First, Second, Third, Sustain, Release, Done };

		struct StageTargets
		{
			std::array<float, 4> levels{};
			std::array<float, 4> steps{}; // per control block
		};

		struct OperatorState
		{
			StageTargets targets{};
			Stage stage{ Stage::First };
			float level{};
			float outputAttenuation{};
			float ampModScale{};
			float frequency{};
			float gain{};
			float gainStep{};
			float output{};
			std::uint32_t phase{};
			std::uint32_t increment{};
			bool fixedFrequency{};
			bool active{};
		};

		static Stage next_stage(Stage stage) noexcept;
		void step_operator(OperatorState& op, std::uint64_t blocks) noexcept;
		void step_pitch(std::uint64_t blocks) noexcept;
		void step_lfo(std::uint64_t blocks) noexcept;
		float lfo_value() const noexcept;
		float lfo_fade() const noexcept;
		void update_increments(float semitones) noexcept;
		void update_control() noexcept;
		void skip_samples(std::uint32_t count) noexcept;

		std::array<OperatorState, fm_operator_count> m_operators{};
		StageTargets m_pitchTargets{};
		EnvelopeState m_envelope{};
		const FmAlgorithm* m_algorithm{};
		const float* m_sine{};
		Stage m_pitchStage{ Stage::First };
		FmLfoWave m_lfoWave{ FmLfoWave::Triangle };
		double m_blockSeconds{};
		double m_elapsedSeconds{};
		double m_lfoPhase{};
		double m_lfoCycleStep{};
		double m_lfoDelaySeconds{};
		double m_lfoFadeSeconds{};
		std::uint32_t m_lfoCycle{};
		std::uint32_t m_lfoSeed{};
		std::uint32_t m_sampleRate{};
		std::uint32_t m_blockRemaining{};
		float m_pitchSemitones{};
		float m_lfoPitchSemitones{};
		float m_lfoAmpAttenuation{};
		float m_feedbackScale{};
		float m_feedbackNewest{};
		float m_feedbackOldest{};
		float m_gain{};
		float m_dcBlock{};
		float m_dcInput{};
		float m_dcOutput{};
		bool m_dcPrimed{ true };
		bool m_valid{};
	};
}
