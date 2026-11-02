#include "axeng/core/audio/audio_sample.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>

#include "axeng/core/resource_loader.h"
#include "spdlog/spdlog.h"

namespace
{
	void append_u16(std::vector<std::uint8_t>& output, std::uint16_t value)
	{
		output.push_back(static_cast<std::uint8_t>(value));
		output.push_back(static_cast<std::uint8_t>(value >> 8));
	}

	void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value)
	{
		for (std::uint32_t shift{}; shift < 32; shift += 8)
			output.push_back(static_cast<std::uint8_t>(value >> shift));
	}
}

ax::AudioSample::AudioSample(Badge<AudioSampleManager>, const std::string& name, std::vector<std::uint8_t> data)
	: Asset{ name }
{
	auto decoded{ audio::decode_audio_sample(data) };
	if (!decoded)
	{
		spdlog::error("Audio sample '{}' is empty, unsupported or invalid", name);
		return;
	}

	m_pcm = std::make_unique<audio::AudioSampleData>(std::move(*decoded));
	m_loaded = true;
}

bool ax::AudioSample::is_supported_extension(const std::string& path) noexcept
{
	auto ext{ std::filesystem::path{ path }.extension().string() };
	std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac";
}

std::optional<std::vector<std::uint8_t>> ax::AudioSample::encode_wav(
	const audio::AudioSampleData& pcm, std::string& error)
{
	error.clear();
	if (!pcm.valid())
	{
		error = "audio sample PCM is invalid";
		return std::nullopt;
	}
	if (pcm.samples.size() > (std::numeric_limits<std::uint32_t>::max() - 36u) / 2u)
	{
		error = "audio sample is too large for a WAV file";
		return std::nullopt;
	}

	const auto dataBytes{ static_cast<std::uint32_t>(pcm.samples.size() * 2) };
	const auto blockAlign{ static_cast<std::uint16_t>(pcm.channels * 2) };
	std::vector<std::uint8_t> wav{};
	wav.reserve(static_cast<std::size_t>(dataBytes) + 44);
	wav.insert(wav.end(), { 'R', 'I', 'F', 'F' });
	append_u32(wav, dataBytes + 36);
	wav.insert(wav.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
	append_u32(wav, 16);
	append_u16(wav, 1);
	append_u16(wav, static_cast<std::uint16_t>(pcm.channels));
	append_u32(wav, pcm.sampleRate);
	append_u32(wav, pcm.sampleRate * blockAlign);
	append_u16(wav, blockAlign);
	append_u16(wav, 16);
	wav.insert(wav.end(), { 'd', 'a', 't', 'a' });
	append_u32(wav, dataBytes);
	for (const auto sample : pcm.samples)
	{
		const auto quantized{ static_cast<std::int16_t>(std::lround(
			std::clamp(sample, -1.0f, 1.0f) * 32767.0f)) };
		append_u16(wav, static_cast<std::uint16_t>(quantized));
	}
	return wav;
}

ax::AudioSampleManager::AudioSampleManager(Badge<Application> badge, ResourceLoader& loader)
	: AssetManager<AudioSample, AudioSampleManager>{ badge, loader }
{
}

std::unique_ptr<ax::AudioSample> ax::AudioSampleManager::load_impl(
	const std::string& name, const AudioSample::Descriptor& description)
{
	auto data{ Resource::load(m_loader, description) };
	if (!data)
	{
		spdlog::error("Failed to load audio sample '{}' from '{}'", name, description);
		return nullptr;
	}
	return std::make_unique<AudioSample>(Badge<AudioSampleManager>{}, name, std::move(*data));
}

std::unique_ptr<ax::AudioSample> ax::AudioSampleManager::load_from_raw_impl(
	const std::string& name, const std::vector<std::uint8_t>& data)
{
	return std::make_unique<AudioSample>(Badge<AudioSampleManager>{}, name, data);
}
