#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "helpers.h"
#include "asset.h"
#include "asset_manager.h"

#include <webgpu/webgpu_cpp.h>
#include <webgpu/webgpu_cpp_print.h>

#include <GLFW/glfw3.h>
#undef APIENTRY

#include "glm/glm.hpp"

#include <imgui.h>

namespace ax
{
	using rectf = glm::vec4;

	struct SpriteGpuData
	{
		glm::vec2 pos{ 0.0f, 0.0f };
		glm::vec2 scale{ 1.0f, 1.0f };
		rectf region{ 0.0f, 0.0f, 0.0f, 0.0f };
		glm::vec4 tint{ 1.0f, 1.0f, 1.0f, 1.0f };
		float z{ 0.0f };
		float rotation{ 0.0f };
		std::uint32_t useRegion{ 0 };
		std::uint32_t padding{ 0 };

		static constexpr std::size_t gpuDataSize{ 64 };
	};

	struct SpriteDefinition
	{
		SpriteGpuData gpuData{};
		Texture* tex{ nullptr };
		Texture* groupedTexture{ nullptr };
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
	static_assert(offsetof(SpriteGpuData, padding) == 60);
	static_assert(sizeof(SpriteGpuData) == SpriteGpuData::gpuDataSize);
	static_assert(offsetof(SpriteDefinition, gpuData) == 0);
	static_assert(offsetof(SpriteDefinition, tex) == SpriteGpuData::gpuDataSize);

	class Texture : public Asset
	{
	public:
		DISABLE_COPY_AND_MOVE(Texture);

		using Descriptor = std::string;

		Texture(Badge<TextureManager>, const std::string& name, const std::vector<uint8_t> data, wgpu::Device& device);
		~Texture();

		const wgpu::Texture& texture() const { return m_texture; }
		const wgpu::TextureView& view() const { return m_view; }
		const ImTextureID imgui_view() const { return (ImTextureID)(intptr_t)m_view.Get(); }
		uint32_t width() const { return m_width; }
		uint32_t height() const { return m_height; }

		GLFWimage create_glfw_image() const;

	private:
		wgpu::Texture m_texture;
		wgpu::TextureView m_view;
		void* m_stbiPtr{ nullptr };

		uint32_t m_width{ 0 };
		uint32_t m_height{ 0 };
	};

	class TextureManager : public AssetManager<Texture, TextureManager>
	{
	public:
		TextureManager(Badge<Application>, ResourceLoader& loader);
		void set_device(wgpu::Device& device);

	private:
		wgpu::Device* m_device;

		std::unique_ptr<Texture> load_from_raw_impl(const std::string& name, const std::vector<uint8_t>& data);
		std::unique_ptr<Texture> load_impl(const std::string& name, const Texture::Descriptor& description);
		friend AssetManager<Texture, TextureManager>;
	};
}