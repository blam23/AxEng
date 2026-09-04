#include "texture.h"
#include <memory>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "spdlog/spdlog.h"

ax::Texture::Texture(Badge<TextureManager>, const std::string& name, const std::vector<uint8_t> imageData, wgpu::Device& device)
	: Asset{ name }
{
	int iwidth, iheight, channels;
	void* data{ nullptr };
	data = stbi_load_from_memory(imageData.data(), (int)imageData.size(), &iwidth, &iheight, &channels, 0);
	if (data)
	{
		uint32_t width{ static_cast<uint32_t>(iwidth) };
		uint32_t height{ static_cast<uint32_t>(iheight) };

		m_stbiPtr = data;

		// Create texture
		wgpu::TextureDescriptor textureDesc
		{
			.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
			.dimension = wgpu::TextureDimension::e2D,
			.size = { width, height },
			.format = wgpu::TextureFormat::BC1RGBAUnorm,
			.mipLevelCount = 1,
			.sampleCount = 1,
		};
		m_texture = device.CreateTexture(&textureDesc);

		// Copy data to texture
		wgpu::TexelCopyTextureInfo copyInfo
		{
			.texture = m_texture,
			.mipLevel = 0,
			.origin = { 0, 0, 0 },
			.aspect = wgpu::TextureAspect::All,
		};
		wgpu::TexelCopyBufferLayout layout
		{
			.offset = 0,
			.bytesPerRow = 4 * width,
			.rowsPerImage = height
		};

		wgpu::Extent3D writeSize { width, height, 1 };
		device.GetQueue().WriteTexture(&copyInfo, data, width * height * 4, &layout, &writeSize);

		m_loaded = true;
	}
	else
	{
		spdlog::error("Failed to load texture, not valid.");
	}
}

ax::Texture::~Texture()
{
	if(m_stbiPtr)
		stbi_image_free(m_stbiPtr);
}

ax::TextureManager::TextureManager(Badge<Module> badge, IResourceLoader& loader)
	: AssetManager<Texture> { badge, loader }
{
}

std::unique_ptr<ax::Texture> ax::TextureManager::inner_load(const std::string& name, const Texture::Descriptor& description)
{
	auto res{ m_loader.load(description.path) };

	if (res.has_value())
		return std::make_unique<Texture>(Badge<TextureManager>{}, name, res.value(), description.device);
	else
		return nullptr;
}
