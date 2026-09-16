#pragma once

#include "helpers.h"

#include <string_view>
#include <filesystem>
#include <expected>
#include <variant>
#include <map>
#include <optional>
#include <typeindex>

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

		DirectoryResourceLoader(DirectoryResourceLoader&& o) noexcept { m_root = o.m_root; }
		DirectoryResourceLoader& operator=(DirectoryResourceLoader&& o) noexcept { m_root = o.m_root; return *this; }

		DirectoryResourceLoader(std::filesystem::path rootDirectory);
		std::expected<std::vector<uint8_t>, ResourceLoadError> load(ResourceID id) const;
		std::optional<ResourceLoadError> setup() { return {}; }
		std::optional<ResourceLoadError>  cleanup() { return {}; }

	private:
		std::filesystem::path m_root;
	};

	struct EmbeddedResource
	{
		const uint8_t* ptr;
		size_t size;
	};

	using EmbeddedResourceLayout = std::map<ResourceID, EmbeddedResource>;

	class EmbeddedResourceLoader
	{
	public:
		DISABLE_COPY(EmbeddedResourceLoader);

		EmbeddedResourceLoader(EmbeddedResourceLoader&& o) noexcept { m_layout = o.m_layout; }
		EmbeddedResourceLoader& operator=(EmbeddedResourceLoader&& o) noexcept { m_layout = o.m_layout;  return *this; }

		EmbeddedResourceLoader(EmbeddedResourceLayout&& layout)
			: m_layout{ std::move(layout) }
		{
		}

		EmbeddedResourceLoader()
			: m_layout{}
		{
		}

		void register_resource(ResourceID, EmbeddedResource);
		std::expected<std::vector<uint8_t>, ResourceLoadError> load(ResourceID id) const;
		std::optional<ResourceLoadError> setup() { return {}; }
		std::optional<ResourceLoadError> cleanup() { return {}; }
		const EmbeddedResourceLayout& layout() const { return m_layout; }

	private:
		EmbeddedResourceLayout m_layout;
	};

	class ZipResourceLoader
	{
	public:
		DISABLE_COPY(ZipResourceLoader);

		ZipResourceLoader(ZipResourceLoader&&) noexcept;
		ZipResourceLoader& operator=(ZipResourceLoader&&) noexcept;

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

		template <typename T>
		static std::expected<std::vector<uint8_t>, ax::ResourceLoadError> embedded_load(ResourceID id)
		{
			const auto ret{ s_embeddedLoader[typeid(T)].load(id)};

			if (ret.has_value())
				return ret.value();
			else
				return std::unexpected{ ret.error() };
		}

		template <typename T>
		static std::expected<std::string, ax::ResourceLoadError> embedded_load_as_text(ResourceID id)
		{
			const auto ret{ embedded_load<T>(id) };

			if (ret.has_value())
				return std::string{ ret.value().begin(), ret.value().end() };
			else
				return std::unexpected{ ret.error() };
		}

		template <typename T>
		static void register_embedded_resource(ResourceID id, EmbeddedResource res)
		{
			s_embeddedLoader[typeid(T)].register_resource(id, res);
		}

		template <typename T>
		static const EmbeddedResourceLoader& get_embedded()
		{
			return s_embeddedLoader[typeid(T)];
		}

	private:
		inline static std::map<std::type_index, EmbeddedResourceLoader> s_embeddedLoader{};
	};
}