#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
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
#include "glm/gtc/packing.hpp"

#include <imgui.h>

namespace ax
{
	class SpriteGroup;
	class LightRenderer;
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

	// Bit flags of SpriteData::useRegion.
	inline constexpr std::uint32_t sprite_flag_region{ 1u };
	// Drawn after lighting is applied, like every screen space sprite.
	inline constexpr std::uint32_t sprite_flag_unlit{ 8u };

	// A sprite as the engine and scripts author it. Sprites are packed into SpriteGpuData for the GPU.
	struct SpriteData
	{
		glm::vec2 pos{ 0.0f, 0.0f };
		glm::vec2 scale{ 1.0f, 1.0f };
		rectf region{ 0.0f, 0.0f, 0.0f, 0.0f };
		glm::vec4 tint{ 1.0f, 1.0f, 1.0f, 1.0f };
		float z{ 0.0f };
		float rotation{ 0.0f };
		// Bit flags: sprite_flag_region uses `region`, sprite_flag_unlit draws after lighting.
		std::uint32_t useRegion{ 0 };
		std::uint32_t screenSpace{ 0 };
		// Screen pixels; a max below the min disables clipping.
		glm::vec4 clipRect{ 0.0f, 0.0f, -1.0f, -1.0f };
	};

	// Distance-field effect of one sprite.
	struct SpriteEffect
	{
		// 0 none, 1 outline, 2 shadow.
		float kind{ 0.0f };
		float width{ 0.0f };
		float softness{ 0.0f };
		// Screen-pixel sampling offset for drop shadows.
		glm::vec2 offset{ 0.0f };
	};

	// The instance layout of the sprite shaders. Regions and clip rectangles are whole texels and pixels (u16), tints
	// are half floats. Whether a sprite is screen space is a pipeline constant, and distance fields have their own
	// shader and buffer, so none of that is stored per sprite.
	struct SpriteGpuData
	{
		glm::vec2 pos{ 0.0f, 0.0f };
		glm::vec2 scale{ 1.0f, 1.0f };
		// x | y << 16 and w | h << 16 in texels; a zero size means the whole texture.
		glm::uvec2 region{ 0u, 0u };
		// r | g << 16 and b | a << 16.
		glm::uvec2 tint{ 0x3C003C00u, 0x3C003C00u };
		// min x | y << 16 and max x | y << 16 in screen pixels; the default clips nothing.
		glm::uvec2 clip{ 0u, 0xFFFFFFFFu };
		float z{ 0.0f };
		float rotation{ 0.0f };

		static constexpr std::size_t gpuDataSize{ 48 };
	};

	struct SdfSpriteGpuData : SpriteGpuData
	{
		// Bit 0 is MSDF (distance in the median of rgb rather than alpha); bits 1-2 are the effect kind.
		std::uint32_t flags{ 0 };
		// Half floats: thickness | softness << 16.
		std::uint32_t effect{ 0 };
		// Half floats: screen pixel offset x | y << 16.
		std::uint32_t effectOffset{ 0 };
		std::uint32_t reserved{ 0 };

		static constexpr std::size_t gpuDataSize{ 64 };
	};

	inline std::uint32_t pack_u16_pair(float low, float high) noexcept
	{
		const auto pack = [](float value)
		{
			return static_cast<std::uint32_t>(std::clamp(std::lround(value), 0l, 65535l));
		};
		return pack(low) | pack(high) << 16;
	}

	inline SpriteGpuData pack_sprite(const SpriteData& data) noexcept
	{
		SpriteGpuData result{};
		result.pos = data.pos;
		result.scale = data.scale;
		result.z = data.z;
		result.rotation = data.rotation;
		if ((data.useRegion & sprite_flag_region) != 0)
			result.region = { pack_u16_pair(data.region.x, data.region.y), pack_u16_pair(data.region.z, data.region.w) };
		result.tint = { glm::packHalf2x16({ data.tint.r, data.tint.g }), glm::packHalf2x16({ data.tint.b, data.tint.a }) };
		if (data.clipRect.z >= data.clipRect.x && data.clipRect.w >= data.clipRect.y)
			result.clip = { pack_u16_pair(data.clipRect.x, data.clipRect.y), pack_u16_pair(data.clipRect.z, data.clipRect.w) };
		return result;
	}

	inline SdfSpriteGpuData pack_sdf_sprite(const SpriteData& data, const SpriteEffect& effect, bool msdf) noexcept
	{
		SdfSpriteGpuData result{};
		static_cast<SpriteGpuData&>(result) = pack_sprite(data);
		const auto kind{ static_cast<std::uint32_t>(std::clamp(effect.kind, 0.0f, 3.0f)) };
		result.flags = (msdf ? 1u : 0u) | kind << 1;
		result.effect = glm::packHalf2x16({ effect.width, effect.softness });
		result.effectOffset = glm::packHalf2x16(effect.offset);
		return result;
	}

	inline bool sprite_unlit(const SpriteData& data) noexcept
	{
		return data.screenSpace != 0 || (data.useRegion & sprite_flag_unlit) != 0;
	}

	struct SpriteDefinition
	{
		SpriteData gpuData{};
		Texture* tex{ nullptr };
		Texture* groupedTexture{ nullptr };
		SpriteGroup* group{ nullptr };
		std::size_t groupIndex{};
		std::size_t activeIndex{};
		bool allocated{ false };
		std::uint64_t generation{};
		std::shared_ptr<Texture> keepAlive{};
		SpriteEffect effect{};
	};

	static_assert(sizeof(glm::vec2) == 2 * sizeof(float));
	static_assert(sizeof(glm::vec4) == 4 * sizeof(float));
	static_assert(offsetof(SpriteGpuData, pos) == 0);
	static_assert(offsetof(SpriteGpuData, scale) == 8);
	static_assert(offsetof(SpriteGpuData, region) == 16);
	static_assert(offsetof(SpriteGpuData, tint) == 24);
	static_assert(offsetof(SpriteGpuData, clip) == 32);
	static_assert(offsetof(SpriteGpuData, z) == 40);
	static_assert(offsetof(SpriteGpuData, rotation) == 44);
	static_assert(sizeof(SpriteGpuData) == SpriteGpuData::gpuDataSize);
	static_assert(sizeof(SdfSpriteGpuData) == SdfSpriteGpuData::gpuDataSize);
	static_assert(offsetof(SpriteDefinition, gpuData) == 0);
	static_assert(offsetof(SpriteDefinition, tex) == sizeof(SpriteData));

	class Texture : public Asset
	{
	public:
		DISABLE_COPY_AND_MOVE(Texture);

		using Descriptor = std::string;

		Texture(Badge<TextureManager>, const std::string& name, const std::vector<uint8_t> data, wgpu::Device* device);
		Texture(Badge<TextureManager>, const std::string& name, uint32_t width, uint32_t height, std::vector<uint8_t> rgbaPixels, wgpu::Device* device);
		Texture(Badge<TextureManager>, const std::string& name, uint32_t width, uint32_t height, wgpu::Device* device);
		// A GPU-only render target in a non-default format.
		Texture(Badge<LightRenderer>, const std::string& name, uint32_t width, uint32_t height, wgpu::TextureFormat format, wgpu::Device* device);
		~Texture();

		// Calls the callback for every live texture, for the debug texture viewer. The registry is locked while the
		// callback runs, so it must not create or destroy textures.
		static void for_each_live(const std::function<void(Texture&)>& callback);

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

		// Distance field textures (e.g. the SDF font) are drawn by the distance-field sprite shader.
		bool sdf() const { return m_sdf; }
		void set_sdf(bool sdf) { m_sdf = sdf; }
		bool msdf() const { return m_msdf; }
		void set_msdf(bool msdf) { m_msdf = msdf; }
		// Drawn by the distance-field sprite shader instead of the ordinary one.
		bool distance_field() const { return m_sdf || m_msdf; }

		// Instance data for drawing a sprite with this texture.
		SpriteGpuData gpu_data_for(const SpriteData& data) const
		{
			return pack_sprite(data);
		}

		SdfSpriteGpuData sdf_gpu_data_for(const SpriteDefinition& sprite) const
		{
			return pack_sdf_sprite(sprite.gpuData, sprite.effect, m_msdf);
		}

	private:
		void upload(const std::string& name, const void* rgbaPixels, uint32_t bytesPerRow, wgpu::Device& device);
		void register_live();

		inline static std::mutex s_liveMutex{};
		inline static std::vector<Texture*> s_live{};

		wgpu::TextureFormat m_format{ wgpu::TextureFormat::RGBA8Unorm };
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
