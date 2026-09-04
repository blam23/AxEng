#pragma once

#include <vector>

#include "helpers.h"
#include "asset.h"
#include "asset_manager.h"

#include <webgpu/webgpu_cpp.h>
#include <webgpu/webgpu_cpp_print.h>

namespace ax
{
	class Texture : public Asset
	{
	public:
		DISABLE_COPY(Texture);

		struct Descriptor
		{
			std::string path;
			wgpu::Device& device;
		};

		Texture(Badge<TextureManager>, const std::string& name, const std::vector<uint8_t> data, wgpu::Device& device);
		~Texture();

	private:
		wgpu::Texture m_texture;
		void* m_stbiPtr{ nullptr };
	};

	class TextureManager : public AssetManager<Texture>
	{
	public:
		TextureManager(Badge<Module>, IResourceLoader& loader);
		virtual std::unique_ptr<Texture> inner_load(const std::string& name, const Texture::Descriptor& description) override;
	};
}