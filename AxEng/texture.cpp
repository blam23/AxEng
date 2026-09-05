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
			.label = name.data(),
			.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
			.dimension = wgpu::TextureDimension::e2D,
			.size = { .width = width, .height = height, .depthOrArrayLayers = 1 },
			.format = wgpu::TextureFormat::RGBA8Unorm,
			.mipLevelCount = 1,
			.sampleCount = 1,
		};
		m_texture = device.CreateTexture(&textureDesc);

		// Create view
		wgpu::TextureViewDescriptor viewDesc
		{
			.label = name.data(),
			.format = wgpu::TextureFormat::RGBA8Unorm,
			.dimension = wgpu::TextureViewDimension::e2D,
			.baseMipLevel = 0,
			.mipLevelCount = 1,
			.baseArrayLayer = 0,
			.arrayLayerCount = 1,
			.aspect = wgpu::TextureAspect::All
		};
		m_view = m_texture.CreateView(&viewDesc);

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
			.bytesPerRow = channels * width,
			.rowsPerImage = height
		};

		wgpu::Extent3D writeSize { width, height, 1 };
		device.GetQueue().WriteTexture(&copyInfo, data, static_cast<size_t>(layout.bytesPerRow) * layout.rowsPerImage, &layout, &writeSize);

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
	: AssetManager<Texture>{ badge, loader }
{
}

std::unique_ptr<ax::Texture> ax::TextureManager::inner_load(const std::string& name, const Texture::Descriptor& description)
{
	if (m_device == nullptr)
	{
		spdlog::error("TextureManager device not set.");
		return nullptr;
	}

	auto res{ m_loader.load(description) };

	if (res.has_value())
		return std::make_unique<Texture>(Badge<TextureManager>{}, name, res.value(), *m_device);
	else
		return nullptr;
}

void ax::TextureManager::set_device(wgpu::Device& device)
{
	m_device = &device;
}
