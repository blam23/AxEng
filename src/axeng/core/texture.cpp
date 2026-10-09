#include "axeng/core/texture.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>

#pragma warning(push)
#pragma warning(disable : 4505) // unused functions due to PNG only flag
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include "axeng/external/stb_image.h"
#define STB_RECT_PACK_IMPLEMENTATION
#include "axeng/external/stb_rect_pack.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "axeng/external/stb_image_write.h"
#pragma warning(pop)

#include <fstream>

#include "spdlog/spdlog.h"

ax::Texture::Texture(Badge<TextureManager>, const std::string& name, const std::vector<uint8_t> imageData, wgpu::Device* device)
	: Asset{ name }
{
	int iwidth, iheight, channels;
	void* data{ nullptr };
	data = stbi_load_from_memory(imageData.data(), (int)imageData.size(), &iwidth, &iheight, &channels, 0);
	if (data)
	{
		m_width = static_cast<uint32_t>(iwidth);
		m_height = static_cast<uint32_t>(iheight);

		m_stbiPtr = data;
		m_channels = static_cast<uint32_t>(channels);

		if (device)
			upload(name, data, static_cast<uint32_t>(channels) * m_width, *device);

		m_loaded = true;
	}
	else
	{
		spdlog::error("Failed to load texture, not valid.");
	}
}

ax::Texture::Texture(Badge<TextureManager>, const std::string& name, uint32_t width, uint32_t height, std::vector<uint8_t> rgbaPixels, wgpu::Device* device)
	: Asset{ name }, m_pixels{ std::move(rgbaPixels) }, m_width{ width }, m_height{ height }
{
	if (width == 0 || height == 0 || m_pixels.size() != static_cast<size_t>(width) * height * 4)
	{
		spdlog::error("Failed to create texture '{}', pixel data does not match {}x{} RGBA.", name, width, height);
		return;
	}

	if (device)
		upload(name, m_pixels.data(), 4 * m_width, *device);

	m_loaded = true;
}

void ax::Texture::upload(const std::string& name, const void* rgbaPixels, uint32_t bytesPerRow, wgpu::Device& device)
{
	// Create texture
	wgpu::TextureDescriptor textureDesc
	{
		.label = name.data(),
		.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
		.dimension = wgpu::TextureDimension::e2D,
		.size = { .width = m_width, .height = m_height, .depthOrArrayLayers = 1 },
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
		.bytesPerRow = bytesPerRow,
		.rowsPerImage = m_height
	};

	wgpu::Extent3D writeSize { m_width, m_height, 1 };
	device.GetQueue().WriteTexture(&copyInfo, rgbaPixels, static_cast<size_t>(layout.bytesPerRow) * layout.rowsPerImage, &layout, &writeSize);
}

ax::Texture::~Texture()
{
	if(m_stbiPtr)
		stbi_image_free(m_stbiPtr);
}

GLFWimage ax::Texture::create_glfw_image() const
{
	return 
	{
		.width = static_cast<int>(m_width),
		.height = static_cast<int>(m_height),
		.pixels = m_stbiPtr ? (unsigned char*)m_stbiPtr : const_cast<unsigned char*>(m_pixels.data()),
	};
}

ax::Error ax::Texture::save_png(const std::filesystem::path& path) const
{
	const void* pixels{ m_stbiPtr ? m_stbiPtr : static_cast<const void*>(m_pixels.data()) };
	if (!m_loaded || pixels == nullptr || m_width == 0 || m_height == 0)
	{
		spdlog::error("Cannot save texture '{}', no pixel data available.", m_name);
		return Error::InvalidTexture;
	}

	// Encode to memory then write ourselves so non-ASCII (wide) paths work on Windows
	std::vector<uint8_t> encoded{};
	const int ok{ stbi_write_png_to_func(
		[](void* ctx, void* data, int size)
		{
			auto* out{ static_cast<std::vector<uint8_t>*>(ctx) };
			const auto* bytes{ static_cast<const uint8_t*>(data) };
			out->insert(out->end(), bytes, bytes + size);
		},
		&encoded,
		static_cast<int>(m_width), static_cast<int>(m_height), static_cast<int>(m_channels),
		pixels, static_cast<int>(m_width * m_channels)) };

	if (!ok)
	{
		spdlog::error("Failed to encode texture '{}' as PNG.", m_name);
		return Error::GenericFailure;
	}

	std::error_code ec{};
	if (path.has_parent_path())
		std::filesystem::create_directories(path.parent_path(), ec);

	std::ofstream file{ path, std::ios::binary | std::ios::trunc };
	if (!file || !file.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size())))
	{
		spdlog::error("Failed to write texture '{}' to '{}'.", m_name, path.string());
		return Error::IO;
	}

	return Error::Success;
}

std::optional<ax::rectf> ax::Texture::region(const std::string& subTexture) const
{
	const auto itr{ m_regions.find(subTexture) };
	if (itr == m_regions.end())
		return std::nullopt;
	return itr->second;
}

void ax::Texture::add_region(const std::string &subTexture, const rectf &region)
{
	m_regions[subTexture] = region;
}

ax::TextureManager::TextureManager(Badge<Application> badge, ResourceLoader& loader)
	: AssetManager<Texture, TextureManager>{ badge, loader }
{
}

ax::Error ax::TextureManager::create_texture_atlas(const Texture::Descriptor &name, const std::vector<Texture::Descriptor> &textureDescs, float minPadding)
{
	// Guaranteed minimum for WebGPU's maxTextureDimension2D
	constexpr uint32_t maxDimension{ 8192 };
	constexpr uint32_t bpp{ 4 };

	std::lock_guard lock{ m_loadMutex };

	if (m_store.contains(name))
	{
		spdlog::error("Cannot create texture atlas '{}', an asset with that name already exists.", name);
		return Error::InvalidTexture;
	}

	struct Image
	{
		std::string desc;
		uint32_t width{ 0 };
		uint32_t height{ 0 };
		std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels{ nullptr, &stbi_image_free };
		uint32_t x{ 0 };
		uint32_t y{ 0 };
	};

	// Decode every source image as RGBA8 on the CPU (no need to upload them individually)
	std::vector<Image> images{};
	images.reserve(textureDescs.size());
	for (const auto& desc : textureDescs)
	{
		if (std::ranges::any_of(images, [&](const Image& img) { return img.desc == desc; }))
			continue;

		auto res{ Resource::load(m_loader, desc) };
		if (!res.has_value())
		{
			spdlog::error("Texture atlas '{}': failed to load resource '{}'.", name, desc);
			return Error::AssetNotFound;
		}

		int w, h, channels;
		stbi_uc* data{ stbi_load_from_memory(res->data(), static_cast<int>(res->size()), &w, &h, &channels, static_cast<int>(bpp)) };
		if (data == nullptr || w <= 0 || h <= 0)
		{
			if (data)
				stbi_image_free(data);
			spdlog::error("Texture atlas '{}': '{}' is not a valid image.", name, desc);
			return Error::InvalidTexture;
		}

		Image img{ .desc = desc, .width = static_cast<uint32_t>(w), .height = static_cast<uint32_t>(h) };
		img.pixels.reset(data);
		images.push_back(std::move(img));
	}

	if (images.empty())
	{
		spdlog::error("Texture atlas '{}': no textures given.", name);
		return Error::InvalidTexture;
	}

	if (images.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
	{
		spdlog::error("Texture atlas '{}': too many images to pack.", name);
		return Error::InvalidTexture;
	}

	if (!std::isfinite(minPadding) || minPadding > maxDimension / 2.0f)
	{
		spdlog::error("Texture atlas '{}': invalid padding {}.", name, minPadding);
		return Error::InvalidTexture;
	}

	// Each image gets `pad` pixels of its own on every side, so neighbours are 2 * pad apart
	const uint32_t pad{ static_cast<uint32_t>(std::ceil(std::max(minPadding, 0.0f))) };

	uint64_t totalArea{ 0 };
	uint32_t widestCell{ 0 };
	for (auto& img : images)
	{
		const uint64_t cw{ img.width + 2ull * pad };
		const uint64_t ch{ img.height + 2ull * pad };
		if (cw > maxDimension || ch > maxDimension)
		{
			spdlog::error("Texture atlas '{}': image '{}' with padding exceeds the maximum dimension of {}.", name, img.desc, maxDimension);
			return Error::InvalidTexture;
		}

		totalArea += cw * ch;
		widestCell = std::max(widestCell, static_cast<uint32_t>(cw));
	}
	if (totalArea > static_cast<uint64_t>(maxDimension) * maxDimension)
	{
		spdlog::error("Texture atlas '{}': total padded image area exceeds the maximum texture area.", name);
		return Error::InvalidTexture;
	}

	const uint32_t initialWidth{ std::max(widestCell, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(totalArea))))) };
	uint32_t finalWidth{ 0 };
	uint32_t finalHeight{ 0 };
	bool packed{ false };
	for (uint32_t packWidth{ initialWidth };;)
	{
		std::vector<stbrp_rect> rects{};
		rects.reserve(images.size());
		for (size_t i{ 0 }; i < images.size(); ++i)
		{
			const auto& img{ images[i] };
			rects.push_back({
				.id = static_cast<int>(i),
				.w = static_cast<int>(img.width + 2 * pad),
				.h = static_cast<int>(img.height + 2 * pad)
			});
		}

		std::vector<stbrp_node> nodes(packWidth);
		stbrp_context context{};
		stbrp_init_target(&context, static_cast<int>(packWidth), static_cast<int>(maxDimension), nodes.data(), static_cast<int>(nodes.size()));
		if (stbrp_pack_rects(&context, rects.data(), static_cast<int>(rects.size())))
		{
			for (const auto& rect : rects)
			{
				auto& img{ images[static_cast<size_t>(rect.id)] };
				img.x = static_cast<uint32_t>(rect.x) + pad;
				img.y = static_cast<uint32_t>(rect.y) + pad;
				finalWidth = std::max(finalWidth, static_cast<uint32_t>(rect.x + rect.w));
				finalHeight = std::max(finalHeight, static_cast<uint32_t>(rect.y + rect.h));
			}

			packed = true;
			break;
		}

		if (packWidth == maxDimension)
			break;
		packWidth = std::min(maxDimension, packWidth + std::max(1u, packWidth / 8));
	}

	if (!packed)
	{
		spdlog::error("Texture atlas '{}': images could not be packed within the maximum dimension of {}.", name, maxDimension);
		return Error::InvalidTexture;
	}

	// Blit, extruding each image's edge pixels into its padding to avoid sampling bleed
	std::vector<uint8_t> atlas(static_cast<size_t>(finalWidth) * finalHeight * bpp, 0);
	for (const auto& img : images)
	{
		const int64_t x0{ static_cast<int64_t>(img.x) - pad }, y0{ static_cast<int64_t>(img.y) - pad };
		const int64_t x1{ static_cast<int64_t>(img.x) + img.width + pad }, y1{ static_cast<int64_t>(img.y) + img.height + pad };

		for (int64_t dy = y0; dy < y1; ++dy)
		{
			const int64_t sy{ std::clamp<int64_t>(dy - img.y, 0, img.height - 1) };
			for (int64_t dx = x0; dx < x1; ++dx)
			{
				const int64_t sx{ std::clamp<int64_t>(dx - img.x, 0, img.width - 1) };
				const auto* src{ img.pixels.get() + (sy * img.width + sx) * bpp };
				auto* dst{ atlas.data() + (dy * finalWidth + dx) * bpp };
				std::memcpy(dst, src, bpp);
			}
		}
	}

	auto texture{ std::make_unique<Texture>(Badge<TextureManager>{}, name, finalWidth, finalHeight, std::move(atlas), m_device) };
	if (!texture->is_loaded())
		return Error::InvalidTexture;

	for (const auto& img : images)
	{
		texture->m_regions[img.desc] = rectf{
			static_cast<float>(img.x), static_cast<float>(img.y),
			static_cast<float>(img.width), static_cast<float>(img.height) };
	}

	spdlog::info("Created texture atlas '{}' ({}x{}) from {} textures.", name, finalWidth, finalHeight, images.size());

	m_store.try_emplace(name, AssetStore{ name, std::move(texture) });
	return Error::Success;
}

std::unique_ptr<ax::Texture> ax::TextureManager::load_from_raw_impl(const std::string &name, const std::vector<uint8_t> &data)
{
	return std::make_unique<Texture>(Badge<TextureManager>{}, name, data, m_device);
}

std::unique_ptr<ax::Texture> ax::TextureManager::load_impl(const std::string& name, const Texture::Descriptor& description)
{
	auto res{ Resource::load(m_loader, description) };

	if (res.has_value())
		return std::make_unique<Texture>(Badge<TextureManager>{}, name, res.value(), m_device);
	else
		return nullptr;
}

void ax::TextureManager::set_device(wgpu::Device& device)
{
	m_device = &device;
}
