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

std::expected<std::vector<uint8_t>, ax::ResourceLoadError> ax::DirectoryResourceLoader::load(ResourceID id) const
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

std::expected<std::vector<uint8_t>, ax::ResourceLoadError> ax::EmbeddedResourceLoader::load(ResourceID id) const
{
	std::vector<uint8_t> ret{};
	const auto idx{ m_layout.find(id) };
	if (idx != m_layout.end())
	{
		const auto entry{ idx->second };
		ret.resize(entry.size);
		memcpy_s(ret.data(), ret.size(), entry.ptr, entry.size);
		return ret;
	}

	return std::unexpected{ ax::ResourceLoadError::NotFound };
}

std::expected<std::vector<uint8_t>, ax::ResourceLoadError> ax::Resource::load(const ResourceLoader& loader, ResourceID id)
{
	const auto ret{ std::visit([&id](auto l) { return l.load(id); }, loader) };

	if (ret.has_value())
		return ret.value();
	else
		return std::unexpected{ ret.error() };
}

std::expected<std::string, ax::ResourceLoadError> ax::Resource::load_as_text(const ResourceLoader& loader, ResourceID id)
{
	const auto ret{ load(loader, id) };

	if (ret.has_value())
		return std::string{ ret.value().begin(), ret.value().end() };
	else
		return std::unexpected{ ret.error() };
}
