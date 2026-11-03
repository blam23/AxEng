#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace ax
{
	// Disk backed store for Dawn's blob cache (compiled shaders and pipelines), one file per entry.
	// Dawn may call it from several threads, and several processes may share the directory, so
	// entries are written to a temporary file and renamed into place.
	class GpuBlobCache
	{
	public:
		explicit GpuBlobCache(std::filesystem::path directory);

		// Returns the size of the entry for |key| when |value| is empty, otherwise the number of
		// bytes copied into |value|. Zero means a cache miss.
		size_t load(std::span<const std::byte> key, std::span<std::byte> value) const;
		void store(std::span<const std::byte> key, std::span<const std::byte> value);

		const std::filesystem::path& directory() const noexcept { return m_directory; }

	private:
		std::filesystem::path entry_path(std::span<const std::byte> key) const;
		void trim();

		std::filesystem::path m_directory;
		std::atomic<uint64_t> m_tempCounter{ 0 };
	};
}
