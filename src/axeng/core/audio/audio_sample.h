#pragma once

#include "axeng/core/asset.h"
#include "axeng/core/asset_manager.h"
#include "axeng/core/audio/instrument.h"
#include "axeng/core/helpers.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ax
{
	class AudioSampleManager;

	class AudioSample : public Asset
	{
	public:
		DISABLE_COPY_AND_MOVE(AudioSample);

		using Descriptor = std::string;

		AudioSample(Badge<AudioSampleManager>, const std::string& name, std::vector<std::uint8_t> data);

		const audio::AudioSampleData& pcm() const noexcept { return *m_pcm; }

		static bool is_supported_extension(const std::string& path) noexcept;
		static std::optional<std::vector<std::uint8_t>> encode_wav(
			const audio::AudioSampleData& pcm, std::string& error);

	private:
		std::unique_ptr<audio::AudioSampleData> m_pcm{};
	};

	class AudioSampleManager : public AssetManager<AudioSample, AudioSampleManager>
	{
	public:
		AudioSampleManager(Badge<Application>, ResourceLoader& loader);

	private:
		std::unique_ptr<AudioSample> load_impl(const std::string& name, const AudioSample::Descriptor& description);
		std::unique_ptr<AudioSample> load_from_raw_impl(const std::string& name, const std::vector<std::uint8_t>& data);
		friend AssetManager<AudioSample, AudioSampleManager>;
	};
}
