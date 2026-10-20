#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "axeng/core/asset.h"
#include "axeng/core/asset_manager.h"
#include "axeng/core/error.h"
#include "axeng/core/helpers.h"

#include <webgpu/webgpu_cpp.h>
#include <webgpu/webgpu_cpp_print.h>

#include <GLFW/glfw3.h>
#undef APIENTRY

#include "glm/glm.hpp"

#include <imgui.h>

namespace ax
{
	class SpriteGroup;
	using rectf = glm::vec4;

	Error encode_png
	(
		uint32_t width,
		uint32_t height,
		uint32_t channels,
		const void* pixels,
		uint32_t bytesPerRow,
		std::vector<uint8_t>& encoded
	);

	Error write_png
	(
		const std::filesystem::path& path,
		uint32_t width,
		uint32_t height,
		uint32_t channels,
		const void* pixels,
		uint32_t bytesPerRow
	);

	struct SpriteGpuData
	{
		glm::vec2 pos{ 0.0f, 0.0f };
		glm::vec2 scale{ 1.0f, 1.0f };
		rectf region{ 0.0f, 0.0f, 0.0f, 0.0f };
		glm::vec4 tint{ 1.0f, 1.0f, 1.0f, 1.0f };
		float z{ 0.0f };
		float rotation{ 0.0f };
		// Bit flags: sprite_flag_region uses `region`; SDF/MSDF flags select their distance channels.
		std::uint32_t useRegion{ 0 };
		std::uint32_t screenSpace{ 0 };
		glm::vec4 clipRect{ 0.0f, 0.0f, -1.0f, -1.0f };

		static constexpr std::size_t gpuDataSize{ 80 };
	};

	inline constexpr std::uint32_t sprite_flag_region{ 1u };
	inline constexpr std::uint32_t sprite_flag_sdf{ 2u };
	inline constexpr std::uint32_t sprite_flag_msdf{ 4u };

	struct SpriteDefinition
	{
		SpriteGpuData gpuData{};
		Texture* tex{ nullptr };
		Texture* groupedTexture{ nullptr };
		SpriteGroup* group{ nullptr };
		std::size_t groupIndex{};
		std::size_t activeIndex{};
		bool allocated{ false };
		std::uint64_t generation{};
		std::shared_ptr<Texture> keepAlive{};
	};

	static_assert(sizeof(glm::vec2) == 2 * sizeof(float));
	static_assert(sizeof(glm::vec4) == 4 * sizeof(float));
	static_assert(offsetof(SpriteGpuData, pos) == 0);
	static_assert(offsetof(SpriteGpuData, scale) == 8);
	static_assert(offsetof(SpriteGpuData, region) == 16);
	static_assert(offsetof(SpriteGpuData, tint) == 32);
	static_assert(offsetof(SpriteGpuData, z) == 48);
	static_assert(offsetof(SpriteGpuData, rotation) == 52);
	static_assert(offsetof(SpriteGpuData, useRegion) == 56);
	static_assert(offsetof(SpriteGpuData, screenSpace) == 60);
	static_assert(offsetof(SpriteGpuData, clipRect) == 64);
	static_assert(sizeof(SpriteGpuData) == SpriteGpuData::gpuDataSize);
	static_assert(offsetof(SpriteDefinition, gpuData) == 0);
	static_assert(offsetof(SpriteDefinition, tex) == SpriteGpuData::gpuDataSize);

	class Texture : public Asset
	{
	public:
		DISABLE_COPY_AND_MOVE(Texture);

		using Descriptor = std::string;

		Texture(Badge<TextureManager>, const std::string& name, const std::vector<uint8_t> data, wgpu::Device* device);
		Texture(Badge<TextureManager>, const std::string& name, uint32_t width, uint32_t height, std::vector<uint8_t> rgbaPixels, wgpu::Device* device);
		Texture(Badge<TextureManager>, const std::string& name, uint32_t width, uint32_t height, wgpu::Device* device);
		~Texture();

		const wgpu::Texture& texture() const { return m_texture; }
		const wgpu::TextureView& view() const { return m_view; }
		const ImTextureID imgui_view() const { return (ImTextureID)(intptr_t)m_view.Get(); }
		uint32_t width() const { return m_width; }
		uint32_t height() const { return m_height; }

		GLFWimage create_glfw_image() const;

		Error save_png(const std::filesystem::path& path) const;

		std::optional<rectf> region(const std::string& subTexture) const;
		const std::map<std::string, rectf>& regions() const { return m_regions; }

		void add_region(const std::string& subTexture, const rectf& region);

		// Distance field textures (e.g. the SDF font) are drawn with sprite_flag_sdf.
		bool sdf() const { return m_sdf; }
		void set_sdf(bool sdf) { m_sdf = sdf; }
		bool msdf() const { return m_msdf; }
		void set_msdf(bool msdf) { m_msdf = msdf; }

		// Sprite data for drawing with this texture, with texture dependent flags applied.
		SpriteGpuData gpu_data_for(const SpriteGpuData& data) const
		{
			auto result{ data };
			if (m_sdf)
				result.useRegion |= sprite_flag_sdf;
			if (m_msdf)
				result.useRegion |= sprite_flag_msdf;
			return result;
		}

	private:
		void upload(const std::string& name, const void* rgbaPixels, uint32_t bytesPerRow, wgpu::Device& device);

		wgpu::Texture m_texture;
		wgpu::TextureView m_view;
		wgpu::Device m_device;
		void* m_stbiPtr{ nullptr };
		std::vector<uint8_t> m_pixels{};
		std::map<std::string, rectf> m_regions{};
		bool m_gpuModified{ false };
		bool m_sdf{ false };
		bool m_msdf{ false };

		friend class TextureManager;
		friend class Window;

		uint32_t m_width{ 0 };
		uint32_t m_height{ 0 };
		uint32_t m_channels{ 4 };
	};

	class TextureManager : public AssetManager<Texture, TextureManager>
	{
	public:
		TextureManager(Badge<Application>, ResourceLoader& loader);
		void set_device(wgpu::Device& device);

		// Packs all given textures into a single texture stored under `name`.
		// Sub-texture pixel regions can be queried with Texture::region(descriptor).
		Error create_texture_atlas(const Texture::Descriptor& name, const std::vector<Texture::Descriptor>& textures, float minPadding = 2.0f);
		std::unique_ptr<Texture> create_temporary(const std::string& name, std::vector<uint8_t> rgbaPixels, float width, float height);
		// Decodes a PNG into a temporary texture. When the image is larger than maxWidth x maxHeight (0 = no limit)
		// it is box-filtered down by the smallest whole factor that makes it fit.
		std::unique_ptr<Texture> create_temporary_from_image(const std::string& name, const std::vector<uint8_t>& encoded, uint32_t maxWidth, uint32_t maxHeight);

	private:
		wgpu::Device* m_device{ nullptr };

		std::unique_ptr<Texture> load_from_raw_impl(const std::string& name, const std::vector<uint8_t>& data);
		std::unique_ptr<Texture> load_impl(const std::string& name, const Texture::Descriptor& description);
		friend AssetManager<Texture, TextureManager>;
	};
}