#pragma once

#include <string_view>
#include <filesystem>
#include <expected>

namespace ax
{
	using ResourceID = std::string_view;

	enum class ResourceLoadError
	{
		Unknown,
		NotFound,
		CantOpen,
	};

	/// <summary>
	/// For getting raw resource file data. No caching, etc. just loading.
	/// This could be straight from a root directory, an archive, a url, a dream, etc.
	/// </summary>
	class IResourceLoader
	{
	public:
		virtual ~IResourceLoader() = default;
		virtual std::expected<std::vector<uint8_t>, ResourceLoadError> load(ResourceID id) = 0;
		std::expected<std::string, ResourceLoadError> load_as_text(ResourceID id);
	private:
	};

	class DirectoryResourceLoader : public IResourceLoader
	{
	public:
		virtual ~DirectoryResourceLoader() = default;
		DirectoryResourceLoader(std::filesystem::path rootDirectory);
		virtual std::expected<std::vector<uint8_t>, ResourceLoadError> load(ResourceID id) override;

	private:
		std::filesystem::path m_root;
	};

	template <typename T>
	concept ValidLoader = requires
	{
		std::derived_from<T, IResourceLoader>;
	};
}