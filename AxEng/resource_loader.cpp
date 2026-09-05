#include "resource_loader.h"

#include "spdlog/spdlog.h"

#include <iostream>
#include <fstream>
#include <string>

#include "log_timer.h"

ax::DirectoryResourceLoader::DirectoryResourceLoader(std::filesystem::path rootDirectory)
	: m_root{ rootDirectory }
{
}

std::expected<std::vector<uint8_t>, ax::ResourceLoadError> ax::DirectoryResourceLoader::load(ResourceID id)
{
	LogTimer _timer{ id };

	std::vector<uint8_t> ret{};

	const auto path{ m_root / id };
	if (!std::filesystem::exists(path))
	{
		spdlog::error("File does not exist: {}", id);
		return std::unexpected{ ax::ResourceLoadError::NotFound };
	}

	// Open file at END of stream (ios::ate)
	std::ifstream file{};
	file.open(path, std::ios::binary | std::ios::ate | std::ios::in);

	if (!file.is_open())
	{
		spdlog::error("Failed to open file: '{}'", id);

		if (file.bad())
			spdlog::error("IO error, badbit is set");

		if (file.fail())
		{
			char err[1024];
			spdlog::error("IO error: {}", strerror_s(err, 1024, errno));
		}

		return std::unexpected{ ax::ResourceLoadError::CantOpen };
	}

	// Create byte vector of length <EOF>
	ret.resize(file.tellg(), 0);

	// Read all file data
	file.seekg(0, std::ios::beg);
	file.read(reinterpret_cast<char*>(ret.data()), ret.size());

	return ret;
}

std::expected<std::string, ax::ResourceLoadError>  ax::IResourceLoader::load_as_text(ResourceID id)
{
	const auto ret{ load(id) };

	if (ret.has_value())
		return std::string{ ret.value().begin(), ret.value().end() };
	else
		return std::unexpected{ ret.error() };
}
