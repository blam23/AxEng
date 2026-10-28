#include "axeng/core/texture.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>

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

ax::Error ax::encode_png
(
	uint32_t width,
	uint32_t height,
	uint32_t channels,
	const void* pixels,
	uint32_t bytesPerRow,
	std::vector<uint8_t>& encoded
)
{
	encoded.clear();
	if (width == 0 || height == 0 || channels == 0 || channels > 4 || pixels == nullptr)
		return Error::InvalidTexture;

	const int ok{ stbi_write_png_to_func(
		[](void* ctx, void* data, int size)
		{
			auto* out{ static_cast<std::vector<uint8_t>*>(ctx) };
			const auto* bytes{ static_cast<const uint8_t*>(data) };
			out->insert(out->end(), bytes, bytes + size);
		},
		&encoded,
		static_cast<int>(width), static_cast<int>(height), static_cast<int>(channels),
		pixels, static_cast<int>(bytesPerRow)) };

	if (!ok)
	{
		encoded.clear();
		return Error::GenericFailure;
	}

	return Error::Success;
}

ax::Error ax::write_png
(
	const std::filesystem::path& path,
	uint32_t width,
	uint32_t height,
	uint32_t channels,
	const void* pixels,
	uint32_t bytesPerRow
)
{
	if (width == 0 || height == 0 || channels == 0 || channels > 4 || pixels == nullptr)
	{
		spdlog::error("Cannot write PNG '{}', invalid image data.", path.string());
		return Error::InvalidTexture;
	}

	std::vector<uint8_t> encoded{};
	const auto encodeResult{ encode_png(width, height, channels, pixels, bytesPerRow, encoded) };
	if (encodeResult != Error::Success)
	{
		spdlog::error("Failed to encode '{}' as PNG.", path.string());
		return encodeResult;
	}

	std::error_code ec{};
	if (path.has_parent_path())
		std::filesystem::create_directories(path.parent_path(), ec);

	std::ofstream file{ path, std::ios::binary | std::ios::trunc };
	if (!file || !file.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size())))
	{
		spdlog::error("Failed to write PNG to '{}'.", path.string());
		return Error::IO;
	}

	return Error::Success;
}

ax::Texture::Texture(Badge<TextureManager>, const std::string& name, const std::vector<uint8_t> imageData, wgpu::Device* device)
	: Asset{ name }
{
	register_live();
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
	register_live();
	if (width == 0 || height == 0 || m_pixels.size() != static_cast<size_t>(width) * height * 4)
	{
		spdlog::error("Failed to create texture '{}', pixel data does not match {}x{} RGBA.", name, width, height);
		return;
	}

	if (device)
		upload(name, m_pixels.data(), 4 * m_width, *device);

	m_loaded = true;
}

ax::Texture::Texture(Badge<TextureManager>, const std::string& name, uint32_t width, uint32_t height, wgpu::Device* device)
	: Asset{ name }, m_width{ width }, m_height{ height }
{
	register_live();
	if (width == 0 || height == 0 || device == nullptr)
	{
		spdlog::error("Failed to create temporary texture '{}', invalid dimensions or device.", name);
		return;
	}

	upload(name, nullptr, 0, *device);
	if (m_texture.Get() == nullptr || m_view.Get() == nullptr)
	{
		spdlog::error("Failed to create temporary texture '{}'.", name);
		return;
	}
	m_loaded = true;
}

ax::Texture::Texture(Badge<LightRenderer>, const std::string& name, uint32_t width, uint32_t height, wgpu::TextureFormat format, wgpu::Device* device)
	: Asset{ name }, m_format{ format }, m_width{ width }, m_height{ height }
{
	register_live();
	if (width == 0 || height == 0 || device == nullptr)
	{
		spdlog::error("Failed to create render target '{}', invalid dimensions or device.", name);
		return;
	}

	upload(name, nullptr, 0, *device);
	if (m_texture.Get() == nullptr || m_view.Get() == nullptr)
	{
		spdlog::error("Failed to create render target '{}'.", name);
		return;
	}
	m_loaded = true;
}

void ax::Texture::register_live()
{
	std::lock_guard lock{ s_liveMutex };
	s_live.push_back(this);
}

void ax::Texture::for_each_live(const std::function<void(Texture&)>& callback)
{
	std::lock_guard lock{ s_liveMutex };
	for (auto* texture : s_live)
		callback(*texture);
}

void ax::Texture::upload(const std::string& name, const void* rgbaPixels, uint32_t bytesPerRow, wgpu::Device& device)
{
	m_device = device;

	// Create texture
	wgpu::TextureDescriptor textureDesc
	{
		.label = name.data(),
		.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst
			| wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::RenderAttachment,
		.dimension = wgpu::TextureDimension::e2D,
		.size = { .width = m_width, .height = m_height, .depthOrArrayLayers = 1 },
		.format = m_format,
		.mipLevelCount = 1,
		.sampleCount = 1,
	};
	m_texture = device.CreateTexture(&textureDesc);

	// Create view
	wgpu::TextureViewDescriptor viewDesc
	{
		.label = name.data(),
		.format = m_format,
		.dimension = wgpu::TextureViewDimension::e2D,
		.baseMipLevel = 0,
		.mipLevelCount = 1,
		.baseArrayLayer = 0,
		.arrayLayerCount = 1,
		.aspect = wgpu::TextureAspect::All
	};
	m_view = m_texture.CreateView(&viewDesc);

	if (rgbaPixels == nullptr)
		return;

	std::vector<uint8_t> convertedPixels{};
	if (m_channels != 4)
	{
		const auto* sourcePixels{ static_cast<const uint8_t*>(rgbaPixels) };
		convertedPixels.resize(static_cast<size_t>(m_width) * m_height * 4);
		for (size_t i{ 0 }, pixelCount{ static_cast<size_t>(m_width) * m_height }; i < pixelCount; ++i)
		{
			const auto* source{ sourcePixels + i * m_channels };
			auto* destination{ convertedPixels.data() + i * 4 };
			destination[0] = source[0];
			destination[1] = m_channels <= 2 ? source[0] : source[1];
			destination[2] = m_channels <= 2 ? source[0] : source[2];
			destination[3] = m_channels == 2 ? source[1] : 255;
		}
		rgbaPixels = convertedPixels.data();
		bytesPerRow = 4 * m_width;
	}

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
	{
		std::lock_guard lock{ s_liveMutex };
		std::erase(s_live, this);
	}
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
	if (!m_loaded || m_width == 0 || m_height == 0)
	{
		spdlog::error("Cannot save texture '{}', no pixel data available.", m_name);
		return Error::InvalidTexture;
	}
	if (m_format != wgpu::TextureFormat::RGBA8Unorm)
	{
		spdlog::error("Cannot save texture '{}', only RGBA8 textures can be saved.", m_name);
		return Error::InvalidTexture;
	}

	std::vector<uint8_t> gpuPixels{};
	uint32_t outputChannels{ m_channels };
	const bool hasCpuPixels{ m_stbiPtr != nullptr || !m_pixels.empty() };
	const void* pixels{ m_stbiPtr ? m_stbiPtr : static_cast<const void*>(m_pixels.data()) };
	if (m_gpuModified || !hasCpuPixels)
	{
		outputChannels = 4;
		if (m_device.Get() == nullptr || m_texture.Get() == nullptr)
		{
			spdlog::error("Cannot read back texture '{}', no GPU device is available.", m_name);
			return Error::InvalidTexture;
		}

		const uint64_t rowBytes{ static_cast<uint64_t>(m_width) * 4 };
		const uint64_t paddedRowBytes{ (rowBytes + 255) & ~uint64_t{ 255 } };
		const uint64_t bufferSize{ paddedRowBytes * m_height };
		if (paddedRowBytes > std::numeric_limits<uint32_t>::max())
		{
			spdlog::error("Cannot read back texture '{}', row size exceeds WebGPU limits.", m_name);
			return Error::InvalidTexture;
		}

		wgpu::BufferDescriptor readbackDesc{};
		readbackDesc.size = bufferSize;
		readbackDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
		readbackDesc.label = "Texture PNG readback";
		auto readback{ m_device.CreateBuffer(&readbackDesc) };

		wgpu::CommandEncoderDescriptor encoderDesc{};
		encoderDesc.label = "Texture PNG readback encoder";
		auto encoder{ m_device.CreateCommandEncoder(&encoderDesc) };
		wgpu::TexelCopyTextureInfo source{};
		source.texture = m_texture;
		wgpu::TexelCopyBufferInfo destination{};
		destination.buffer = readback;
		destination.layout.bytesPerRow = static_cast<uint32_t>(paddedRowBytes);
		destination.layout.rowsPerImage = m_height;
		const wgpu::Extent3D extent{ m_width, m_height, 1 };
		encoder.CopyTextureToBuffer(&source, &destination, &extent);
		auto command{ encoder.Finish() };
		m_device.GetQueue().Submit(1, &command);

		struct MapResult
		{
			std::atomic<bool> complete{ false };
			std::atomic<bool> succeeded{ false };
		};
		const auto result{ std::make_shared<MapResult>() };
		readback.MapAsync(wgpu::MapMode::Read, 0, bufferSize, wgpu::CallbackMode::AllowSpontaneous,
			[result](wgpu::MapAsyncStatus status, wgpu::StringView)
			{
				result->succeeded.store(status == wgpu::MapAsyncStatus::Success, std::memory_order_relaxed);
				result->complete.store(true, std::memory_order_release);
			});

		const auto deadline{ std::chrono::steady_clock::now() + std::chrono::seconds{ 10 } };
		while (!result->complete.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
		{
			m_device.Tick();
			std::this_thread::yield();
		}
		if (!result->complete.load(std::memory_order_acquire))
		{
			spdlog::error("Timed out reading back texture '{}' for PNG saving.", m_name);
			return Error::GenericFailure;
		}
		if (!result->succeeded.load(std::memory_order_relaxed))
		{
			spdlog::error("Failed to map texture '{}' for PNG saving.", m_name);
			return Error::GenericFailure;
		}

		const auto* mapped{ static_cast<const uint8_t*>(readback.GetConstMappedRange()) };
		if (mapped == nullptr)
		{
			readback.Unmap();
			spdlog::error("Failed to access texture '{}' readback data.", m_name);
			return Error::GenericFailure;
		}
		gpuPixels.resize(static_cast<size_t>(rowBytes) * m_height);
		for (uint32_t y{ 0 }; y < m_height; ++y)
			std::memcpy(gpuPixels.data() + static_cast<size_t>(y) * rowBytes,
				mapped + static_cast<size_t>(y) * paddedRowBytes, static_cast<size_t>(rowBytes));
		readback.Unmap();
		pixels = gpuPixels.data();
	}

	if (pixels == nullptr)
	{
		spdlog::error("Cannot save texture '{}', no pixel data available.", m_name);
		return Error::InvalidTexture;
	}

	return write_png(path, m_width, m_height, outputChannels, pixels, m_width * outputChannels);
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
		std::vector<stbi_uc> pixels{};
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

		if (desc == "__builtin_font")
		{
			const auto font{ m_store.find(desc) };
			if (font == m_store.end())
			{
				spdlog::error("Texture atlas '{}': generated font texture '{}' is not loaded.", name, desc);
				return Error::AssetNotFound;
			}

			const auto& texture{ *font->second.asset };
			const auto* source{ texture.m_stbiPtr
				? static_cast<const stbi_uc*>(texture.m_stbiPtr)
				: texture.m_pixels.data() };
			if (source == nullptr || texture.m_width == 0 || texture.m_height == 0
				|| texture.m_channels == 0 || texture.m_channels > 4)
			{
				spdlog::error("Texture atlas '{}': generated font texture '{}' has invalid pixels.", name, desc);
				return Error::InvalidTexture;
			}

			Image img{ .desc = desc, .width = texture.m_width, .height = texture.m_height };
			img.pixels.resize(static_cast<size_t>(img.width) * img.height * bpp);
			for (size_t i{}; i < static_cast<size_t>(img.width) * img.height; ++i)
			{
				const auto* sourcePixel{ source + i * texture.m_channels };
				auto* destination{ img.pixels.data() + i * bpp };
				destination[0] = sourcePixel[0];
				destination[1] = texture.m_channels <= 2 ? sourcePixel[0] : sourcePixel[1];
				destination[2] = texture.m_channels <= 2 ? sourcePixel[0] : sourcePixel[2];
				destination[3] = texture.m_channels == 2 ? sourcePixel[1] : 255;
			}
			images.push_back(std::move(img));
			continue;
		}

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
		img.pixels.assign(data, data + static_cast<size_t>(img.width) * img.height * bpp);
		stbi_image_free(data);
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
				const auto* src{ img.pixels.data() + (sy * img.width + sx) * bpp };
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

std::unique_ptr<ax::Texture> ax::TextureManager::create_temporary(const std::string& name, std::vector<uint8_t> rgbaPixels, float width, float height)
{
	if (m_device == nullptr || !std::isfinite(width) || !std::isfinite(height)
		|| width < 1.0f || height < 1.0f || std::floor(width) != width || std::floor(height) != height
		|| static_cast<double>(width) > std::numeric_limits<uint32_t>::max()
		|| static_cast<double>(height) > std::numeric_limits<uint32_t>::max())
	{
		spdlog::error("Failed to create temporary texture '{}', invalid dimensions or device.", name);
		return nullptr;
	}

	const auto textureWidth{ static_cast<uint32_t>(width) };
	const auto textureHeight{ static_cast<uint32_t>(height) };
	wgpu::Limits limits{};
	if (!m_device->GetLimits(&limits) || textureWidth > limits.maxTextureDimension2D || textureHeight > limits.maxTextureDimension2D)
	{
		spdlog::error("Failed to create temporary texture '{}', dimensions {}x{} exceed device limits.", name, textureWidth, textureHeight);
		return nullptr;
	}

	std::unique_ptr<Texture> texture{};
	if (rgbaPixels.empty())
		texture = std::make_unique<Texture>(Badge<TextureManager>{}, name, textureWidth, textureHeight, m_device);
	else
		texture = std::make_unique<Texture>(Badge<TextureManager>{}, name, textureWidth, textureHeight, std::move(rgbaPixels), m_device);

	if (!texture->is_loaded())
		return nullptr;
	return texture;
}

std::unique_ptr<ax::Texture> ax::TextureManager::create_temporary_from_image
(
	const std::string& name,
	const std::vector<uint8_t>& encoded,
	uint32_t maxWidth,
	uint32_t maxHeight
)
{
	if (encoded.empty() || encoded.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
	{
		spdlog::error("Failed to create temporary texture '{}', no image data.", name);
		return nullptr;
	}

	int width{};
	int height{};
	int channels{};
	stbi_uc* const pixels{ stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height, &channels, 4) };
	if (pixels == nullptr || width <= 0 || height <= 0)
	{
		if (pixels != nullptr)
			stbi_image_free(pixels);
		const char* const reason{ stbi_failure_reason() };
		spdlog::error("Failed to decode the image for temporary texture '{}': {}", name, reason ? reason : "unknown error");
		return nullptr;
	}

	const auto sourceWidth{ static_cast<uint32_t>(width) };
	const auto sourceHeight{ static_cast<uint32_t>(height) };
	uint32_t factor{ 1 };
	if (maxWidth > 0)
		factor = std::max(factor, (sourceWidth + maxWidth - 1) / maxWidth);
	if (maxHeight > 0)
		factor = std::max(factor, (sourceHeight + maxHeight - 1) / maxHeight);

	const auto targetWidth{ std::max(1u, sourceWidth / factor) };
	const auto targetHeight{ std::max(1u, sourceHeight / factor) };
	std::vector<uint8_t> rgba(static_cast<size_t>(targetWidth) * targetHeight * 4);
	if (factor == 1)
		std::memcpy(rgba.data(), pixels, rgba.size());
	else
	{
		for (uint32_t ty{ 0 }; ty < targetHeight; ++ty)
		{
			const auto y0{ ty * factor };
			const auto y1{ std::min(y0 + factor, sourceHeight) };
			for (uint32_t tx{ 0 }; tx < targetWidth; ++tx)
			{
				const auto x0{ tx * factor };
				const auto x1{ std::min(x0 + factor, sourceWidth) };
				uint32_t sum[4]{};
				for (auto y{ y0 }; y < y1; ++y)
				{
					const auto* row{ pixels + (static_cast<size_t>(y) * sourceWidth + x0) * 4 };
					for (auto x{ x0 }; x < x1; ++x, row += 4)
					{
						sum[0] += row[0];
						sum[1] += row[1];
						sum[2] += row[2];
						sum[3] += row[3];
					}
				}
				const auto count{ (y1 - y0) * (x1 - x0) };
				auto* const out{ rgba.data() + (static_cast<size_t>(ty) * targetWidth + tx) * 4 };
				for (int c{ 0 }; c < 4; ++c)
					out[c] = static_cast<uint8_t>((sum[c] + count / 2) / count);
			}
		}
	}
	stbi_image_free(pixels);

	return create_temporary(name, std::move(rgba), static_cast<float>(targetWidth), static_cast<float>(targetHeight));
}
