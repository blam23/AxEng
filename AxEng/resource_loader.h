#pragma once

#include <string_view>
#include <filesystem>
#include <expected>
#include <variant>
#include <map>

namespace ax
{
	using ResourceID = std::string_view;

	enum class ResourceLoadError
	{
		Unknown,
		NotFound,
		CantOpen,
	};

	class DirectoryResourceLoader
	{
	public:
		DirectoryResourceLoader(std::filesystem::path rootDirectory);
		std::expected<std::vector<uint8_t>, ResourceLoadError> load(ResourceID id) const;

	private:
		std::filesystem::path m_root;
	};

	struct EmbeddedResource
	{
		uint8_t* ptr;
		size_t size;
	};

	using EmbeddedResourceLayout = std::map<ResourceID, EmbeddedResource>;

	class EmbeddedResourceLoader
	{
	public:
		EmbeddedResourceLoader(EmbeddedResourceLayout&& layout)
			: m_layout{ std::move(layout) }
		{

		}
		std::expected<std::vector<uint8_t>, ResourceLoadError> load(ResourceID id) const;
	private:
		EmbeddedResourceLayout m_layout;
	};

	using ResourceLoader = std::variant<
		DirectoryResourceLoader,
		EmbeddedResourceLoader
	>;

	class Resource
	{
	public:
		static std::expected<std::vector<uint8_t>, ax::ResourceLoadError> load(const ResourceLoader& loader, ResourceID id);
		static std::expected<std::string, ax::ResourceLoadError> load_as_text(const ResourceLoader& loader, ResourceID id);
	};
}