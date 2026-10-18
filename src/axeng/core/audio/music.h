#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "axeng/core/asset.h"
#include "axeng/core/asset_manager.h"
#include "axeng/core/helpers.h"

namespace ax
{
	class MusicManager;

	// An encoded music track (ogg, mp3, flac or wav). The compressed data is kept in memory and streamed
	// through a decoder while it plays.
	class Music : public Asset
	{
	public:
		DISABLE_COPY_AND_MOVE(Music);

		using Descriptor = std::string;

		Music(Badge<MusicManager>, const std::string& name, std::vector<std::uint8_t> data);

		const std::vector<std::uint8_t>& data() const noexcept { return m_data; }
		double length_seconds() const noexcept { return m_length; }
		std::uint32_t sample_rate() const noexcept { return m_sampleRate; }
		std::uint32_t channels() const noexcept { return m_channels; }

		static bool is_supported_extension(const std::string& path) noexcept;

	private:
		std::vector<std::uint8_t> m_data;
		double m_length{ 0.0 };
		std::uint32_t m_sampleRate{ 0 };
		std::uint32_t m_channels{ 0 };
	};

	class MusicManager : public AssetManager<Music, MusicManager>
	{
	public:
		MusicManager(Badge<Application>, ResourceLoader& loader);

	private:
		std::unique_ptr<Music> load_impl(const std::string& name, const Music::Descriptor& description);
		std::unique_ptr<Music> load_from_raw_impl(const std::string& name, const std::vector<std::uint8_t>& data);
		friend AssetManager<Music, MusicManager>;
	};
}
