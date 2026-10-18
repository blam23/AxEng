#include "axeng/core/audio/music.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "miniaudio.h"
#include "spdlog/spdlog.h"

ax::Music::Music(Badge<MusicManager>, const std::string& name, std::vector<std::uint8_t> data)
	: Asset{ name }
	, m_data{ std::move(data) }
{
	if (m_data.empty())
	{
		spdlog::error("Music '{}' is empty", name);
		return;
	}

	ma_decoder decoder{};
	const auto config{ ma_decoder_config_init(ma_format_f32, 0, 0) };
	if (ma_decoder_init_memory(m_data.data(), m_data.size(), &config, &decoder) != MA_SUCCESS)
	{
		spdlog::error("Music '{}' is not a supported audio file (ogg, mp3, flac or wav)", name);
		return;
	}

	ma_format format{};
	ma_uint32 channels{};
	ma_uint32 sampleRate{};
	ma_decoder_get_data_format(&decoder, &format, &channels, &sampleRate, nullptr, 0);
	ma_uint64 frames{};
	ma_decoder_get_length_in_pcm_frames(&decoder, &frames);
	ma_decoder_uninit(&decoder);

	if (channels == 0 || sampleRate == 0)
	{
		spdlog::error("Music '{}' has an invalid format", name);
		return;
	}

	m_channels = channels;
	m_sampleRate = sampleRate;
	m_length = static_cast<double>(frames) / static_cast<double>(sampleRate);
	m_loaded = true;
}

bool ax::Music::is_supported_extension(const std::string& path) noexcept
{
	auto ext{ std::filesystem::path{ path }.extension().string() };
	std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return ext == ".ogg" || ext == ".mp3" || ext == ".flac" || ext == ".wav";
}

ax::MusicManager::MusicManager(Badge<Application> badge, ResourceLoader& loader)
	: AssetManager<Music, MusicManager>{ badge, loader }
{
}

std::unique_ptr<ax::Music> ax::MusicManager::load_impl(const std::string& name, const Music::Descriptor& description)
{
	auto data{ Resource::load(m_loader, description) };
	if (!data.has_value())
	{
		spdlog::error("Failed to load music '{}' from '{}'", name, description);
		return nullptr;
	}
	return std::make_unique<Music>(Badge<MusicManager>{}, name, std::move(data.value()));
}

std::unique_ptr<ax::Music> ax::MusicManager::load_from_raw_impl(const std::string& name, const std::vector<std::uint8_t>& data)
{
	return std::make_unique<Music>(Badge<MusicManager>{}, name, data);
}
