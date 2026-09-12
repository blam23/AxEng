#pragma once

#include <vector>

#include "helpers.h"
#include "asset.h"
#include "asset_manager.h"

#include <webgpu/webgpu_cpp.h>
#include <webgpu/webgpu_cpp_print.h>

#include <GLFW/glfw3.h>
#undef APIENTRY

#include <imgui.h>

namespace ax
{
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

		std::unique_ptr<Texture> load_impl(const std::string& name, const Texture::Descriptor& description);
		friend AssetManager<Texture, TextureManager>;
	};
}