#pragma once

#include <string_view>
#include <filesystem>

namespace ax
{
	using ResourceID = std::string_view;

	/// <summary>
	/// For getting raw resource file data. No caching, etc. just loading.
	/// This could be straight from the filesystem, or from a archive, url, a dream, etc.
	/// </summary>
	class IResourceLoader
	{
	public:
		virtual ~IResourceLoader() = default;
		virtual std::vector<uint8_t> load(ResourceID id) = 0;
	private:
	};

	class FileResourceLoader : public IResourceLoader
	{
	public:
		virtual ~FileResourceLoader() = default;
		FileResourceLoader(std::filesystem::path rootDirectory);
		virtual std::vector<uint8_t> load(ResourceID id);

	private:
		std::filesystem::path m_root;
	};
}