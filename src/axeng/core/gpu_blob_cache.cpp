#include "axeng/core/gpu_blob_cache.h"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <fstream>
#include <vector>
#include <windows.h>

namespace
{
	constexpr std::array<char, 8> s_magic{ 'A', 'X', 'G', 'P', 'U', 'C', '0', '1' };
	constexpr size_t s_header_size{ s_magic.size() + sizeof(uint64_t) };
	constexpr uintmax_t s_max_cache_bytes{ 128ull * 1024 * 1024 };
	constexpr uintmax_t s_trimmed_cache_bytes{ 96ull * 1024 * 1024 };
	constexpr std::string_view s_entry_extension{ ".bin" };

	uint64_t hash_key(std::span<const std::byte> key)
	{
		uint64_t hash{ 14695981039346656037ull };
		for (const auto byte : key)
		{
			hash ^= static_cast<uint8_t>(byte);
			hash *= 1099511628211ull;
		}
		return hash;
	}
}

ax::GpuBlobCache::GpuBlobCache(std::filesystem::path directory)
	: m_directory{ std::move(directory) }
{
	std::error_code error{};
	std::filesystem::create_directories(m_directory, error);
	if (error)
		spdlog::warn("Failed to create the GPU cache directory {}: {}", m_directory.string(), error.message());
	else
		trim();
}

std::filesystem::path ax::GpuBlobCache::entry_path(std::span<const std::byte> key) const
{
	return m_directory / std::format("{:016x}{}", hash_key(key), s_entry_extension);
}

size_t ax::GpuBlobCache::load(std::span<const std::byte> key, std::span<std::byte> value) const
{
	std::ifstream file{ entry_path(key), std::ios::binary | std::ios::ate };
	if (!file)
		return 0;

	const auto fileSize{ static_cast<uint64_t>(file.tellg()) };
	if (fileSize < s_header_size + key.size())
		return 0;
	file.seekg(0);

	std::array<char, s_magic.size()> magic{};
	uint64_t keySize{ 0 };
	file.read(magic.data(), magic.size());
	file.read(reinterpret_cast<char*>(&keySize), sizeof(keySize));
	if (!file || magic != s_magic || keySize != key.size())
		return 0;

	// Different keys can share a file name, so the stored key must match exactly.
	std::vector<std::byte> storedKey(key.size());
	file.read(reinterpret_cast<char*>(storedKey.data()), static_cast<std::streamsize>(storedKey.size()));
	if (!file || !std::ranges::equal(storedKey, key))
		return 0;

	const auto valueSize{ static_cast<size_t>(fileSize - s_header_size - key.size()) };
	if (value.empty())
		return valueSize;
	if (value.size() != valueSize)
		return 0;

	file.read(reinterpret_cast<char*>(value.data()), static_cast<std::streamsize>(value.size()));
	return file ? valueSize : 0;
}

void ax::GpuBlobCache::store(std::span<const std::byte> key, std::span<const std::byte> value)
{
	const auto path{ entry_path(key) };
	auto tempPath{ path };
	tempPath += std::format(".{}.{}.tmp", ::GetCurrentProcessId(), m_tempCounter.fetch_add(1, std::memory_order_relaxed));

	{
		std::ofstream file{ tempPath, std::ios::binary | std::ios::trunc };
		const uint64_t keySize{ key.size() };
		file.write(s_magic.data(), s_magic.size());
		file.write(reinterpret_cast<const char*>(&keySize), sizeof(keySize));
		file.write(reinterpret_cast<const char*>(key.data()), static_cast<std::streamsize>(key.size()));
		file.write(reinterpret_cast<const char*>(value.data()), static_cast<std::streamsize>(value.size()));
		if (!file)
		{
			file.close();
			std::error_code ignored{};
			std::filesystem::remove(tempPath, ignored);
			spdlog::warn("Failed to write GPU cache entry {}", tempPath.string());
			return;
		}
	}

	// Fails when another process has the entry open; that process is using an equivalent entry.
	std::error_code error{};
	std::filesystem::rename(tempPath, path, error);
	if (error)
		std::filesystem::remove(tempPath, error);
}

void ax::GpuBlobCache::trim()
{
	struct Entry
	{
		std::filesystem::path path;
		std::filesystem::file_time_type writeTime;
		uintmax_t size;
	};

	std::vector<Entry> entries{};
	uintmax_t totalSize{ 0 };
	std::error_code error{};
	for (const auto& item : std::filesystem::directory_iterator{ m_directory, error })
	{
		std::error_code itemError{};
		if (!item.is_regular_file(itemError))
			continue;

		const auto& path{ item.path() };
		const auto writeTime{ item.last_write_time(itemError) };
		if (itemError)
			continue;

		// Leftovers from a process that exited mid-write.
		if (path.extension() == ".tmp")
		{
			if (std::filesystem::file_time_type::clock::now() - writeTime > std::chrono::hours{ 1 })
				std::filesystem::remove(path, itemError);
			continue;
		}
		if (path.extension() != s_entry_extension)
			continue;

		const auto size{ item.file_size(itemError) };
		if (itemError)
			continue;
		entries.push_back({ path, writeTime, size });
		totalSize += size;
	}

	if (totalSize <= s_max_cache_bytes)
		return;

	std::ranges::sort(entries, {}, &Entry::writeTime);
	for (const auto& entry : entries)
	{
		if (totalSize <= s_trimmed_cache_bytes)
			break;
		std::error_code removeError{};
		if (std::filesystem::remove(entry.path, removeError))
			totalSize -= entry.size;
	}
	spdlog::info("Trimmed the GPU cache in {} to {} bytes", m_directory.string(), totalSize);
}
