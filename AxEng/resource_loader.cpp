#include "resource_loader.h"

#include "spdlog/spdlog.h"

#include <iostream>
#include <fstream>
#include <string>

#include "log_timer.h"

ax::FileResourceLoader::FileResourceLoader(std::filesystem::path rootDirectory)
	: m_root{ rootDirectory }
{
}

std::vector<uint8_t> ax::FileResourceLoader::load(ResourceID id)
{
	LogTimer _timer{ id };

	std::vector<uint8_t> ret{};

	const auto path{ m_root.append(id) };
	if (!std::filesystem::exists(path))
	{
		spdlog::error("File does not exist: {}", id);
		return ret;
	}

	// Open file at END of stream (ios::ate)
	std::ifstream file{};
	file.open(path, std::ios::binary | std::ios::ate);

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

		return ret;
	}

	// Create byte vector of length <EOF>
	ret.resize(file.tellg(), 0);

	// Read all file data
	file.seekg(0, std::ios::beg);
	file.read(reinterpret_cast<char*>(ret.data()), ret.size());

	return ret;
}
