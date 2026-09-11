#pragma once

#include "helpers.h"

#include <string_view>
#include <filesystem>
#include <expected>
#include <variant>
#include <map>
#include <optional>

namespace ax
{
	using ResourceID = std::string_view;

	enum class ResourceLoadError
	{
		Unknown = 1,
		NotFound,
		CantOpen,
		ReadFailure,
	};

	class DirectoryResourceLoader
	{
	public:
		DISABLE_COPY(DirectoryResourceLoader);

		DirectoryResourceLoader(DirectoryResourceLoader&& o) { m_root = o.m_root; }
		DirectoryResourceLoader& operator=(DirectoryResourceLoader&& o) { m_root = o.m_root; return *this; }

		DirectoryResourceLoader(std::filesystem::path rootDirectory);
		std::expected<std::vector<uint8_t>, ResourceLoadError> load(ResourceID id) const;
		std::optional<ResourceLoadError> setup() { return {}; }
		std::optional<ResourceLoadError>  cleanup() { return {}; }

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
		DISABLE_COPY(EmbeddedResourceLoader);

		EmbeddedResourceLoader(EmbeddedResourceLoader&& o) { m_layout = o.m_layout; }
		EmbeddedResourceLoader& operator=(EmbeddedResourceLoader&& o) { m_layout = o.m_layout;  return *this; }

		EmbeddedResourceLoader(EmbeddedResourceLayout&& layout)
			: m_layout{ std::move(layout) }
		{

		}
		std::expected<std::vector<uint8_t>, ResourceLoadError> load(ResourceID id) const;
		std::optional<ResourceLoadError> setup() { return {}; }
		std::optional<ResourceLoadError>  cleanup() { return {}; }

	private:
		EmbeddedResourceLayout m_layout;
	};

	class ZipResourceLoader
	{
	public:
		DISABLE_COPY(ZipResourceLoader);

		ZipResourceLoader(ZipResourceLoader&&);
		ZipResourceLoader& operator=(ZipResourceLoader&&);

		ZipResourceLoader(std::filesystem::path zipFile);
		std::expected<std::vector<uint8_t>, ResourceLoadError> load(ResourceID id) const;
		std::optional<ResourceLoadError> setup();
		std::optional<ResourceLoadError>  cleanup();

	private:

		std::filesystem::path m_zipFile;
		void* m_fileStream{ nullptr };
		void* m_bufferStream{ nullptr };
		void* m_handle{ nullptr };
	};

	using ResourceLoader = std::variant<
		DirectoryResourceLoader,
		EmbeddedResourceLoader,
		ZipResourceLoader
	>;

	class Resource
	{
	public:
		static std::expected<std::vector<uint8_t>, ax::ResourceLoadError> load(const ResourceLoader& loader, ResourceID id);
		static std::expected<std::string, ax::ResourceLoadError> load_as_text(const ResourceLoader& loader, ResourceID id);
		static std::optional<ax::ResourceLoadError> setup_loader(ResourceLoader& loader);
		static std::optional<ax::ResourceLoadError> cleanup_loader(ResourceLoader& loader);
	};
}