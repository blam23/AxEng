#include "axeng/core/audio/instrument.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "miniaudio.h"

namespace
{
	constexpr float s_min_gain{ 0.0f };
	constexpr float s_max_gain{ 4.0f };
	constexpr float s_max_envelope_seconds{ 120.0f };
	constexpr std::uint64_t s_max_sample_frames{ 300ull * 192000ull };

	bool finite_between(float value, float minimum, float maximum) noexcept
	{
		return std::isfinite(value) && value >= minimum && value <= maximum;
	}

	bool valid_envelope(const ax::audio::InstrumentEnvelope& envelope) noexcept
	{
		return finite_between(envelope.attack, 0.0f, s_max_envelope_seconds)
			&& finite_between(envelope.decay, 0.0f, s_max_envelope_seconds)
			&& finite_between(envelope.sustain, 0.0f, 1.0f)
			&& finite_between(envelope.release, 0.0f, s_max_envelope_seconds);
	}

	bool valid_synth(const ax::audio::SynthInstrumentDefinition& definition) noexcept
	{
		return static_cast<std::uint8_t>(definition.wave) <= static_cast<std::uint8_t>(ax::audio::InstrumentWave::Noise)
			&& finite_between(definition.gain, s_min_gain, s_max_gain)
			&& finite_between(definition.detuneCents, -1200.0f, 1200.0f)
			&& finite_between(definition.vibratoCents, 0.0f, 200.0f)
			&& finite_between(definition.vibratoRate, 0.0f, 20.0f)
			&& finite_between(definition.lowPassHz, 20.0f, 96000.0f)
			&& finite_between(definition.pitchDropSemitones, 0.0f, 48.0f)
			&& finite_between(definition.pitchDropSeconds, 0.0f, 2.0f)
			&& valid_envelope(definition.envelope);
	}

	double integrated_pitch_cycles(const ax::audio::SynthInstrumentDefinition& definition,
		double baseFrequency, double startSeconds, double endSeconds) noexcept
	{
		if (definition.pitchDropSemitones == 0.0f || definition.pitchDropSeconds == 0.0f)
			return baseFrequency * (endSeconds - startSeconds);

		const auto dropDuration{ static_cast<double>(definition.pitchDropSeconds) };
		const auto sweptEnd{ std::min(endSeconds, dropDuration) };
		const auto dropRate{ static_cast<double>(definition.pitchDropSemitones)
			* std::numbers::ln2 / (12.0 * dropDuration) };
		double cycles{};
		if (startSeconds < sweptEnd)
		{
			cycles += baseFrequency * (std::exp(-dropRate * startSeconds)
				- std::exp(-dropRate * sweptEnd)) / dropRate;
		}
		if (endSeconds > dropDuration)
		{
			const auto constantFrequency{ baseFrequency
				* std::exp(-dropRate * dropDuration) };
			cycles += constantFrequency * (endSeconds - std::max(startSeconds, dropDuration));
		}
		return cycles;
	}

	float noise_sample(std::uint64_t position, std::uint8_t seed) noexcept
	{
		auto value{ position + static_cast<std::uint64_t>(seed) + 0x9e3779b97f4a7c15ull };
		value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
		value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
		value ^= value >> 31;
		return static_cast<float>(value >> 40) / 8388607.5f - 1.0f;
	}

	float poly_blep(double phase, double increment) noexcept
	{
		if (phase < increment)
		{
			const auto t{ phase / increment };
			return static_cast<float>(t + t - t * t - 1.0);
		}
		if (phase > 1.0 - increment)
		{
			const auto t{ (phase - 1.0) / increment };
			return static_cast<float>(t * t + t + t + 1.0);
		}
		return 0.0f;
	}

	float oscillator_sample(ax::audio::InstrumentWave wave, double phase, double increment) noexcept
	{
		const auto p{ static_cast<float>(phase) };
		switch (wave)
		{
		case ax::audio::InstrumentWave::Sine:
			return std::sin(p * 2.0f * std::numbers::pi_v<float>);
		case ax::audio::InstrumentWave::Triangle:
			return 1.0f - 4.0f * std::abs(p - 0.5f);
		case ax::audio::InstrumentWave::Saw:
			return static_cast<float>(phase * 2.0 - 1.0) - poly_blep(phase, increment);
		case ax::audio::InstrumentWave::Square:
		{
			auto value{ phase < 0.5 ? 1.0f : -1.0f };
			value += poly_blep(phase, increment);
			auto shifted{ phase + 0.5 };
			if (shifted >= 1.0)
				shifted -= 1.0;
			value -= poly_blep(shifted, increment);
			return value;
		}
		case ax::audio::InstrumentWave::Noise:
			return 0.0f;
		}
		return 0.0f;
	}

	float read_channel(const ax::audio::AudioSampleData& sample, std::uint64_t frame, std::uint32_t channel) noexcept
	{
		const auto index{ static_cast<std::size_t>(frame) * sample.channels + channel };
		return sample.samples[index];
	}

	float interpolate_channel(const ax::audio::AudioSampleData& sample, double position,
		std::uint64_t endFrame, std::uint32_t channel) noexcept
	{
		const auto first{ static_cast<std::uint64_t>(position) };
		const auto second{ std::min(first + 1, endFrame - 1) };
		const auto fraction{ static_cast<float>(position - static_cast<double>(first)) };
		const auto a{ read_channel(sample, first, channel) };
		const auto b{ read_channel(sample, second, channel) };
		return a + (b - a) * fraction;
	}
}

std::size_t ax::audio::AudioSampleData::frame_count() const noexcept
{
	return channels == 0 ? 0 : samples.size() / channels;
}

bool ax::audio::AudioSampleData::metadata_valid() const noexcept
{
	return sampleRate >= 8000 && sampleRate <= 192000 && (channels == 1 || channels == 2) && !samples.empty()
		&& samples.size() % channels == 0 && frame_count() <= s_max_sample_frames
		&& frame_count() <= static_cast<std::uint64_t>(sampleRate) * 300;
}

bool ax::audio::AudioSampleData::valid() const noexcept
{
	return metadata_valid() && std::ranges::all_of(samples, [](float value) { return std::isfinite(value); });
}

std::optional<ax::audio::AudioSampleData> ax::audio::decode_audio_sample(std::span<const std::uint8_t> encoded)
{
	if (encoded.empty())
		return std::nullopt;

	ma_decoder decoder{};
	const auto config{ ma_decoder_config_init(ma_format_f32, 0, 0) };
	if (ma_decoder_init_memory(encoded.data(), encoded.size(), &config, &decoder) != MA_SUCCESS)
		return std::nullopt;

	ma_format format{};
	ma_uint32 channels{};
	ma_uint32 sampleRate{};
	ma_uint64 frameCount{};
	const auto formatResult{ ma_decoder_get_data_format(&decoder, &format, &channels, &sampleRate, nullptr, 0) };
	const auto lengthResult{ ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount) };
	if (formatResult != MA_SUCCESS || lengthResult != MA_SUCCESS || (channels != 1 && channels != 2)
		|| sampleRate < 8000 || sampleRate > 192000 || frameCount == 0 || frameCount > s_max_sample_frames
		|| frameCount > static_cast<ma_uint64>(sampleRate) * 300)
	{
		ma_decoder_uninit(&decoder);
		return std::nullopt;
	}

	if (frameCount > std::numeric_limits<std::size_t>::max() / channels)
	{
		ma_decoder_uninit(&decoder);
		return std::nullopt;
	}

	AudioSampleData result{};
	result.sampleRate = sampleRate;
	result.channels = channels;
	result.samples.resize(static_cast<std::size_t>(frameCount) * channels);
	ma_uint64 readFrames{};
	const auto readResult{ ma_decoder_read_pcm_frames(&decoder, result.samples.data(), frameCount, &readFrames) };
	ma_decoder_uninit(&decoder);
	if (readResult != MA_SUCCESS || readFrames != frameCount || !result.valid())
		return std::nullopt;
	return result;
}

void ax::audio::EnvelopeState::start(const InstrumentEnvelope& envelope, float velocity, std::uint32_t sampleRate) noexcept
{
	m_envelope = envelope;
	m_sampleRate = sampleRate;
	m_velocity = std::clamp(velocity, 0.0f, 1.0f);
	m_stagePosition = 0;
	m_attackFrames = static_cast<std::uint64_t>(std::round(static_cast<double>(envelope.attack) * sampleRate));
	m_decayFrames = static_cast<std::uint64_t>(std::round(static_cast<double>(envelope.decay) * sampleRate));
	m_releaseFrames = static_cast<std::uint64_t>(std::round(static_cast<double>(envelope.release) * sampleRate));
	m_level = 0.0f;
	m_releaseStart = 0.0f;
	m_finished = sampleRate == 0 || !valid_envelope(envelope) || !(m_velocity > 0.0f);
	m_stage = m_finished ? Stage::Finished : (m_attackFrames > 0 ? Stage::Attack
		: (m_decayFrames > 0 ? Stage::Decay : Stage::Sustain));
	if (!m_finished && m_stage == Stage::Decay)
		m_level = 1.0f;
	if (!m_finished && m_stage == Stage::Sustain)
		m_level = envelope.sustain;
}

void ax::audio::EnvelopeState::note_off() noexcept
{
	if (m_finished || m_stage == Stage::Release)
		return;
	m_releaseStart = m_level;
	m_stagePosition = 0;
	if (m_releaseFrames == 0 || !(m_releaseStart > 0.0f))
	{
		m_level = 0.0f;
		m_stage = Stage::Finished;
		m_finished = true;
		return;
	}
	m_stage = Stage::Release;
}

void ax::audio::EnvelopeState::advance(std::uint64_t frames) noexcept
{
	while (!m_finished && frames > 0)
	{
		switch (m_stage)
		{
		case Stage::Attack:
		{
			const auto remaining{ m_attackFrames - m_stagePosition };
			if (frames < remaining)
			{
				m_stagePosition += frames;
				m_level = static_cast<float>(m_stagePosition) / static_cast<float>(m_attackFrames);
				return;
			}
			frames -= remaining;
			m_level = 1.0f;
			m_stagePosition = 0;
			m_stage = m_decayFrames > 0 ? Stage::Decay : Stage::Sustain;
			break;
		}
		case Stage::Decay:
		{
			const auto remaining{ m_decayFrames - m_stagePosition };
			if (frames < remaining)
			{
				m_stagePosition += frames;
				m_level = 1.0f + (m_envelope.sustain - 1.0f)
					* static_cast<float>(m_stagePosition) / static_cast<float>(m_decayFrames);
				return;
			}
			frames -= remaining;
			m_level = m_envelope.sustain;
			m_stagePosition = 0;
			m_stage = Stage::Sustain;
			break;
		}
		case Stage::Sustain:
			m_level = m_envelope.sustain;
			return;
		case Stage::Release:
		{
			const auto remaining{ m_releaseFrames - m_stagePosition };
			if (frames < remaining)
			{
				m_stagePosition += frames;
				m_level = m_releaseStart * (1.0f - static_cast<float>(m_stagePosition)
					/ static_cast<float>(m_releaseFrames));
				return;
			}
			m_level = 0.0f;
			m_stage = Stage::Finished;
			m_finished = true;
			break;
		}
		case Stage::Finished:
			m_finished = true;
			break;
		}
	}
}

float ax::audio::EnvelopeState::next() noexcept
{
	if (m_finished)
		return 0.0f;

	float level{};
	switch (m_stage)
	{
	case Stage::Attack:
		level = static_cast<float>(m_stagePosition + 1) / static_cast<float>(m_attackFrames);
		if (++m_stagePosition >= m_attackFrames)
		{
			m_stage = m_decayFrames > 0 ? Stage::Decay : Stage::Sustain;
			m_stagePosition = 0;
			m_level = 1.0f;
		}
		break;
	case Stage::Decay:
		level = 1.0f + (m_envelope.sustain - 1.0f)
			* static_cast<float>(m_stagePosition + 1) / static_cast<float>(m_decayFrames);
		if (++m_stagePosition >= m_decayFrames)
		{
			m_stage = Stage::Sustain;
			m_stagePosition = 0;
			m_level = m_envelope.sustain;
		}
		break;
	case Stage::Sustain:
		level = m_envelope.sustain;
		break;
	case Stage::Release:
		level = m_releaseStart * (1.0f - static_cast<float>(m_stagePosition + 1)
			/ static_cast<float>(m_releaseFrames));
		if (++m_stagePosition >= m_releaseFrames)
		{
			level = 0.0f;
			m_level = 0.0f;
			m_stage = Stage::Finished;
			m_finished = true;
		}
		break;
	case Stage::Finished:
		m_finished = true;
		break;
	}
	m_level = std::clamp(level, 0.0f, 1.0f);
	return m_level * m_velocity;
}

ax::audio::SynthInstrumentVoice::SynthInstrumentVoice(const SynthInstrumentDefinition& definition,
	std::uint8_t note, float velocity, std::uint32_t sampleRate) noexcept
	: m_definition{ definition }
	, m_sampleRate{ sampleRate }
	, m_noiseSeed{ note }
{
	m_valid = valid_synth(definition) && note <= 127 && sampleRate >= 8000 && sampleRate <= 192000
		&& finite_between(velocity, 0.0f, 1.0f) && velocity > 0.0f;
	if (m_valid)
	{
		m_baseFrequency = 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0
			+ static_cast<double>(definition.detuneCents) / 1200.0);
		m_filterAlpha = 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * definition.lowPassHz
			/ static_cast<float>(sampleRate));
		m_envelope.start(definition.envelope, velocity, sampleRate);
	}
}

void ax::audio::SynthInstrumentVoice::advance(std::uint64_t frames) noexcept
{
	if (!m_valid || frames == 0)
		return;

	const auto sampleRate{ static_cast<double>(m_sampleRate) };
	const auto startSeconds{ static_cast<double>(m_samplePosition) / sampleRate };
	const auto endPosition{ m_samplePosition + frames };
	const auto endSeconds{ static_cast<double>(endPosition) / sampleRate };
	m_phase = std::fmod(m_phase + integrated_pitch_cycles(m_definition, m_baseFrequency,
		startSeconds, endSeconds), 1.0);
	m_samplePosition = endPosition;
	m_vibratoPhase = std::fmod(m_vibratoPhase + 2.0 * std::numbers::pi
		* static_cast<double>(m_definition.vibratoRate) * static_cast<double>(frames) / sampleRate,
		2.0 * std::numbers::pi);
	m_envelope.advance(frames);
}

bool ax::audio::SynthInstrumentVoice::next(float& sample) noexcept
{
	if (!m_valid || m_envelope.finished())
	{
		sample = 0.0f;
		return false;
	}

	auto frequency{ m_baseFrequency };
	if (m_definition.pitchDropSemitones > 0.0f && m_definition.pitchDropSeconds > 0.0f)
	{
		const auto progress{ std::min(1.0, static_cast<double>(m_samplePosition)
			/ (static_cast<double>(m_sampleRate) * m_definition.pitchDropSeconds)) };
		frequency *= std::pow(2.0, -static_cast<double>(m_definition.pitchDropSemitones)
			* progress / 12.0);
	}
	const auto vibrato{ static_cast<double>(m_definition.vibratoCents)
		* std::sin(m_vibratoPhase) };
	frequency *= std::pow(2.0, vibrato / 1200.0);
	const auto increment{ std::min(frequency / static_cast<double>(m_sampleRate), 0.45) };
	auto value{ m_definition.wave == InstrumentWave::Noise
		? noise_sample(m_samplePosition, m_noiseSeed)
		: oscillator_sample(m_definition.wave, m_phase, increment) };
	m_phase += increment;
	if (m_phase >= 1.0)
		m_phase -= 1.0;
	++m_samplePosition;

	if (m_definition.vibratoRate > 0.0f && m_definition.vibratoCents > 0.0f)
	{
		m_vibratoPhase += 2.0 * std::numbers::pi * static_cast<double>(m_definition.vibratoRate)
			/ static_cast<double>(m_sampleRate);
		if (m_vibratoPhase >= 2.0 * std::numbers::pi)
			m_vibratoPhase -= 2.0 * std::numbers::pi;
	}

	if (m_definition.lowPassHz < static_cast<float>(m_sampleRate) * 0.49f)
	{
		m_filterState += m_filterAlpha * (value - m_filterState);
		value = m_filterState;
	}
	sample = value * m_envelope.next() * m_definition.gain;
	return true;
}

std::optional<ax::audio::AudioSampleData> ax::audio::render_voice_sample(
	const VoiceRenderRequest& request, std::string& error)
{
	error.clear();
	const auto holdSeconds{ request.holdSeconds };
	const auto sampleRate{ request.sampleRate };
	if (!std::isfinite(holdSeconds) || !(holdSeconds > 0.0f)
		|| !finite_between(holdSeconds + request.releaseSeconds, 0.0f, synth_sample_max_seconds))
	{
		error = "synth sample hold plus release must be positive and no longer than 30 seconds";
		return std::nullopt;
	}
	if (sampleRate < 8000 || sampleRate > 96000)
	{
		error = "synth sample rate must be between 8000 and 96000 Hz";
		return std::nullopt;
	}
	if (!request.voiceValid)
	{
		error = "synth sample has an invalid instrument definition, note, or velocity";
		return std::nullopt;
	}

	const auto holdFrames{ static_cast<std::uint64_t>(std::llround(
		static_cast<double>(holdSeconds) * static_cast<double>(sampleRate))) };
	const auto releaseFrames{ static_cast<std::uint64_t>(std::llround(
		static_cast<double>(request.releaseSeconds) * static_cast<double>(sampleRate))) };
	if (holdFrames == 0 || holdFrames > std::numeric_limits<std::size_t>::max() - releaseFrames)
	{
		error = "synth sample duration is outside the supported range";
		return std::nullopt;
	}

	AudioSampleData rendered{
		.samples = std::vector<float>(static_cast<std::size_t>(holdFrames + releaseFrames)),
		.sampleRate = sampleRate,
		.channels = 1 };
	for (std::uint64_t frame{}; frame < holdFrames + releaseFrames; ++frame)
	{
		if (frame == holdFrames)
			request.noteOff();
		float sample{};
		if (!request.next(sample))
			break;
		rendered.samples[static_cast<std::size_t>(frame)] = sample;
	}

	float peak{};
	for (const auto sample : rendered.samples)
		peak = std::max(peak, std::abs(sample));
	if (peak > 0.95f)
	{
		const auto gain{ 0.95f / peak };
		for (auto& sample : rendered.samples)
			sample *= gain;
	}
	if (!rendered.valid())
	{
		error = "synth sample renderer produced invalid PCM";
		return std::nullopt;
	}
	return rendered;
}

std::optional<ax::audio::AudioSampleData> ax::audio::render_synth_sample(
	const SynthInstrumentDefinition& definition, std::uint8_t note, float velocity, float holdSeconds,
	std::uint32_t sampleRate, std::string& error)
{
	SynthInstrumentVoice voice{ definition, note, velocity, sampleRate };
	return render_voice_sample({ .holdSeconds = holdSeconds, .releaseSeconds = definition.envelope.release,
		.sampleRate = sampleRate, .voiceValid = voice.valid(),
		.next = [&voice](float& sample) { return voice.next(sample); },
		.noteOff = [&voice] { voice.note_off(); } }, error);
}

ax::audio::SamplerInstrumentVoice::SamplerInstrumentVoice(const AudioSampleData& sample,
	const SamplerInstrumentDefinition& definition, std::uint8_t note, float velocity,
	std::uint32_t outputSampleRate) noexcept
	: m_sample{ &sample }
	, m_definition{ definition }
{
	m_endFrame = definition.endFrame == 0 ? sample.frame_count() : definition.endFrame;
	m_loopStartFrame = definition.loopStartFrame;
	m_loopEndFrame = definition.loopEndFrame;
	m_valid = sample.metadata_valid() && definition.rootNote <= 127 && note <= 127
		&& outputSampleRate >= 8000 && outputSampleRate <= 192000
		&& finite_between(velocity, 0.0f, 1.0f) && velocity > 0.0f
		&& finite_between(definition.tuningCents, -1200.0f, 1200.0f)
		&& finite_between(definition.gain, s_min_gain, s_max_gain)
		&& valid_envelope(definition.envelope)
		&& definition.startFrame < m_endFrame && m_endFrame <= sample.frame_count()
		&& (!definition.loop || (m_loopStartFrame >= definition.startFrame
			&& m_loopStartFrame < m_loopEndFrame && m_loopEndFrame <= m_endFrame));

	if (!m_valid)
		return;

	const auto semitones{ static_cast<double>(static_cast<int>(note) - static_cast<int>(definition.rootNote))
		+ static_cast<double>(definition.tuningCents) / 100.0 };
	m_step = static_cast<double>(sample.sampleRate) / static_cast<double>(outputSampleRate)
		* std::pow(2.0, semitones / 12.0);
	m_valid = std::isfinite(m_step) && m_step >= 1.0 / 256.0 && m_step <= 256.0;
	m_position = static_cast<double>(definition.startFrame);
	if (m_valid)
		m_envelope.start(definition.envelope, velocity, outputSampleRate);
}

void ax::audio::SamplerInstrumentVoice::advance(std::uint64_t frames) noexcept
{
	if (!m_valid || frames == 0)
		return;

	m_position += m_step * static_cast<double>(frames);
	if (m_definition.loop && m_position >= static_cast<double>(m_loopEndFrame))
	{
		const auto loopLength{ static_cast<double>(m_loopEndFrame - m_loopStartFrame) };
		m_position = static_cast<double>(m_loopStartFrame)
			+ std::fmod(m_position - static_cast<double>(m_loopStartFrame), loopLength);
	}
	m_envelope.advance(frames);
}

bool ax::audio::SamplerInstrumentVoice::next(float& left, float& right) noexcept
{
	if (!m_valid || m_envelope.finished() || m_position >= static_cast<double>(m_endFrame))
	{
		left = 0.0f;
		right = 0.0f;
		return false;
	}

	if (m_definition.loop && m_position >= static_cast<double>(m_loopEndFrame))
	{
		const auto loopLength{ static_cast<double>(m_loopEndFrame - m_loopStartFrame) };
		m_position = static_cast<double>(m_loopStartFrame)
			+ std::fmod(m_position - static_cast<double>(m_loopStartFrame), loopLength);
	}

	const auto channel0{ interpolate_channel(*m_sample, m_position, m_endFrame, 0) };
	const auto channel1{ m_sample->channels == 2
		? interpolate_channel(*m_sample, m_position, m_endFrame, 1)
		: channel0 };
	const auto gain{ m_envelope.next() * m_definition.gain };
	left = channel0 * gain;
	right = channel1 * gain;
	m_position += m_step;
	return true;
}
