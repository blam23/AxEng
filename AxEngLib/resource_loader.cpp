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


std::optional<ax::ResourceLoadError> ax::Resource::setup_loader(ResourceLoader& loader)
{
	return std::visit([](auto& l) { return l.setup(); }, loader);
}

std::optional<ax::ResourceLoadError> ax::Resource::cleanup_loader(ResourceLoader& loader)
{
	return std::visit([](auto& l) { return l.cleanup(); }, loader);
}

std::expected<std::vector<uint8_t>, ax::ResourceLoadError> ax::Resource::load(const ResourceLoader& loader, ResourceID id)
{
	const auto ret{ std::visit([&id](const auto& l) { return l.load(id); }, loader) };

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

#include <minizip-ng/mz.h>
#include <minizip-ng/mz_os.h>
#include <minizip-ng/mz_strm.h>
#include <minizip-ng/mz_strm_buf.h>
#include <minizip-ng/mz_strm_os.h>
#include <minizip-ng/mz_zip.h>

ax::ZipResourceLoader::ZipResourceLoader(ZipResourceLoader&& in)
{
	m_bufferStream = in.m_bufferStream;
	m_fileStream = in.m_fileStream;
	m_handle = in.m_handle;
	m_zipFile = in.m_zipFile;

	in.m_bufferStream = nullptr;
	in.m_fileStream = nullptr;
	in.m_handle = nullptr;
	in.m_zipFile = "";
}

ax::ZipResourceLoader& ax::ZipResourceLoader::operator=(ZipResourceLoader&& in)
{
	m_bufferStream = in.m_bufferStream;
	m_fileStream = in.m_fileStream;
	m_handle = in.m_handle;
	m_zipFile = in.m_zipFile;

	in.m_bufferStream = nullptr;
	in.m_fileStream = nullptr;
	in.m_handle = nullptr;
	in.m_zipFile = "";

	return *this;
}

ax::ZipResourceLoader::ZipResourceLoader(std::filesystem::path path)
	: m_zipFile{ path }
{
}

std::optional<ax::ResourceLoadError> ax::ZipResourceLoader::setup()
{
	spdlog::debug("<Res> loader setup");

	if (!std::filesystem::exists(m_zipFile))
	{
		spdlog::error("File does not exist: {}", m_zipFile.string());
		return ax::ResourceLoadError::NotFound;
	}

	m_fileStream = mz_stream_os_create();
	if (!m_fileStream)
	{
		spdlog::error("Failed to open os stream");
		return ax::ResourceLoadError::CantOpen;
	}

	m_bufferStream = mz_stream_buffered_create();
	if (!m_fileStream)
	{
		spdlog::error("Failed to open buffer stream");
		return ax::ResourceLoadError::CantOpen;
	}

	auto err{ mz_stream_set_base(m_bufferStream, m_fileStream) };
	if (err != MZ_OK)
	{
		spdlog::error("Failed to set underlying stream for buffer, err: {}", err);
		return ax::ResourceLoadError::CantOpen;
	}

	err = mz_stream_open(m_bufferStream, m_zipFile.string().c_str(), MZ_OPEN_MODE_READ);
	if (err != MZ_OK)
	{
		spdlog::error("Failed to open zip file, err: {}", err);
		return ax::ResourceLoadError::CantOpen;
	}

	m_handle = mz_zip_create();
	err = mz_zip_open(m_handle, m_fileStream, MZ_OPEN_MODE_READ);
	if (err != MZ_OK)
	{
		spdlog::error("Failed to read zip file, err: {}", err);
		return ax::ResourceLoadError::CantOpen;
	}

	spdlog::debug("<Res> Zip loader setup complete!");

	return {};
}

std::optional<ax::ResourceLoadError> ax::ZipResourceLoader::cleanup()
{
	spdlog::debug("<Res> loader cleanup");

	int err{ MZ_OK };

	if (m_handle)
	{
		err = mz_zip_close(m_handle);
		if (err != MZ_OK)
		{
			spdlog::error("Failed to close zip file, err: {}", err);
			return ax::ResourceLoadError::CantOpen;
		}
	}

	if (m_bufferStream)
	{
		err = mz_stream_close(m_bufferStream);
		if (err != MZ_OK)
		{
			spdlog::error("Failed to close buffer, err: {}", err);
			return ax::ResourceLoadError::CantOpen;
		}
	}

	mz_stream_delete(&m_bufferStream);
	mz_stream_delete(&m_fileStream);

	spdlog::debug("<Res> Zip loader cleanup complete!");

	return {};
}

std::expected<std::vector<uint8_t>, ax::ResourceLoadError> ax::ZipResourceLoader::load(ResourceID path) const
{
	int err{ MZ_OK };

	err = mz_zip_locate_entry(m_handle, path.data(), true);
	if (err != MZ_OK)
	{
		spdlog::error("Unable to open path: '{}', err: {}", path, err);
		return std::unexpected{ ax::ResourceLoadError::NotFound };
	}

	mz_zip_file* entryInfo{ nullptr };
	err = mz_zip_entry_get_info(m_handle, &entryInfo);
	if (err != MZ_OK || entryInfo == nullptr)
	{
		spdlog::error("Failed to get entry info for '{}', err: {}", path, err);
		return std::unexpected{ ax::ResourceLoadError::CantOpen };
	}

	const uint64_t uncompressedSize = entryInfo->uncompressed_size;
	std::vector<uint8_t> ret;
	ret.resize(static_cast<size_t>(uncompressedSize));

	err = mz_zip_entry_read_open(m_handle, 0, nullptr);
	if (err != MZ_OK || entryInfo == nullptr)
	{
		spdlog::error("Failed to open '{}', err: {}", path, err);
		return std::unexpected{ ax::ResourceLoadError::CantOpen };
	}

	size_t totalRead = 0;
	while (totalRead < ret.size())
	{
		const int32_t toRead = static_cast<int32_t>(std::min<size_t>(ret.size() - totalRead, 16384));
		int32_t bytesRead = mz_zip_entry_read(m_handle, ret.data() + totalRead, toRead);

		if (bytesRead < 0)
		{
			spdlog::error("Error reading path '{}', err: {}", path, bytesRead);
			return std::unexpected{ ax::ResourceLoadError::ReadFailure };
		}

		if (bytesRead == 0)
			break;

		totalRead += static_cast<size_t>(bytesRead);
	}

	err = mz_zip_entry_close(m_handle);
	if (err != MZ_OK || entryInfo == nullptr)
	{
		spdlog::error("Failed to close '{}', err: {}", path, err);
		return std::unexpected{ ax::ResourceLoadError::Unknown };
	}

	if (totalRead != ret.size())
		ret.resize(totalRead);

	return ret;
}
