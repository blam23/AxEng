#include "axeng/core/imgui_helper.h"
#include "axeng/core/imgui_style.h"
#include "axeng/core/log_timer.h"
#include "axeng/debug/perf_profiler.h"
#include "axeng/core/window.h"
#include "axeng/core/input/mouse.h"
#include "axeng/core/shapes/debug_shapes.h"
#include "axeng/core/shapes/shape_renderer.h"

#include "spdlog/spdlog.h"

#include "axeng/external/imgui_impl_glfw.h"
#include "axeng/external/imgui_impl_wgpu.h"
#include "axeng/external/IconsFontAwesome6.h"
#include "axeng/external/ImGuiNotify.hpp"
#include <imgui.h>
#include <dawn/native/DawnNative.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <format>
#include <iostream>
#include <limits>
#include <thread>

std::map<GLFWwindow*, ax::Window*> s_windows{};
std::mutex s_windows_mutex{};
bool s_imgui_initialized{ false };

static void glfw_error_callback(int error, const char* description)
{
	spdlog::error("glfw error {}: {}", error, description);
}

ax::Window::Window(const WindowDefinition& def)
	: m_width{ def.width }
	, m_height{ def.height }
	, m_vsync{ def.vsync }
{
	m_camera.set_position({ 0.0f, 0.0f });

	LogTimer _timer{ "create window" };

	glfwWindowHint(GLFW_RESIZABLE, def.resizable ? GLFW_TRUE : GLFW_FALSE);

	m_window = glfwCreateWindow(m_width, m_height, def.title.data(), NULL, NULL);

	std::lock_guard lock{ s_windows_mutex };
	s_windows.emplace(m_window, this);
}

ax::Window::~Window()
{
	if (m_window)
	{
		{
			std::lock_guard lock{ s_windows_mutex };
			s_windows.erase(m_window);

			if (s_windows.size() == 0)
			{
				ShapeRenderer::shutdown();
				if (s_imgui_initialized)
				{
					ImGui_ImplGlfw_Shutdown();
					ImGui_ImplWGPU_Shutdown();
					s_imgui_initialized = false;
				}
			}
		}
		glfwDestroyWindow(m_window);
	}

	if (m_surface.Get() != nullptr)
		m_surface.Unconfigure();
}

void ax::Window::resize_event_handler(GLFWwindow* window, int width, int height)
{
	std::lock_guard lock{ s_windows_mutex };
	auto it = s_windows.find(window);
	if (it != s_windows.end())
	{
		if (it->second == nullptr)
		{
			spdlog::error("Window list contains null window?");
			return;
		}

		it->second->m_width = width == 0 ? 1 : width;
		it->second->m_height = height == 0 ? 1 : height;
		it->second->create_surfaces();
		it->second->m_resizeEventHandler.fire({ static_cast<uint32_t>(it->second->m_width), static_cast<uint32_t>(it->second->m_height) });
	}
}

bool ax::setup_glfw()
{
	LogTimer _timer{ "glfw setup" };

	glfwSetErrorCallback(glfw_error_callback);

	if (!glfwInit())
	{
		spdlog::error("glfw init failed");
		return false;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	
	return true;
}

void ax::teardown_glfw()
{
	glfwTerminate();
}

bool ax::Window::create_surfaces()
{
	//
	// Surface
	//
	wgpu::SurfaceCapabilities capabilities{};
	if (m_surface.GetCapabilities(m_adapter, &capabilities) == wgpu::Status::Success)
		m_surfaceFormat = capabilities.formats[0];
	else
	{
		spdlog::error("Failed to get surface capabilities..");
		return false;
	}
	m_surfaceSupportsCopySource =
		(static_cast<uint32_t>(capabilities.usages) & static_cast<uint32_t>(wgpu::TextureUsage::CopySrc)) != 0;

	auto surfaceUsage{ wgpu::TextureUsage::RenderAttachment };
	if (m_surfaceSupportsCopySource)
		surfaceUsage |= wgpu::TextureUsage::CopySrc;

	wgpu::SurfaceConfiguration config
	{
		.nextInChain = nullptr,
		.device = m_device,
		.format = m_surfaceFormat,
		.usage = surfaceUsage,
		.width = m_width,
		.height = m_height,
		.viewFormatCount = 0,
		.viewFormats = nullptr,
		.alphaMode = wgpu::CompositeAlphaMode::Auto,
		.presentMode = m_vsync ? wgpu::PresentMode::Fifo : wgpu::PresentMode::Immediate,
	};

	m_surface.Configure(&config);

	//
	// Depth Texture
	//
	wgpu::TextureDescriptor depthTextureDesc
	{
		.usage = wgpu::TextureUsage::RenderAttachment,
		.dimension = wgpu::TextureDimension::e2D,
		.size = { m_width, m_height },
		.format = m_depthTextureFormat,
		.mipLevelCount = 1,
		.sampleCount = 4,
		.viewFormatCount = 1,
		.viewFormats = &m_depthTextureFormat,
	};
	m_depthTexture = m_device.CreateTexture(&depthTextureDesc);

	wgpu::TextureViewDescriptor depthTextureViewDesc
	{
		.format = m_depthTextureFormat,
		.dimension = wgpu::TextureViewDimension::e2D,
		.baseMipLevel = 0,
		.mipLevelCount = 1,
		.baseArrayLayer = 0,
		.arrayLayerCount = 1,
		.aspect = wgpu::TextureAspect::DepthOnly,
	};
	m_depthTextureView = m_depthTexture.CreateView(&depthTextureViewDesc);

	wgpu::TextureDescriptor accumulationTextureDesc{};
	accumulationTextureDesc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
	accumulationTextureDesc.dimension = wgpu::TextureDimension::e2D;
	accumulationTextureDesc.size = { m_width, m_height };
	accumulationTextureDesc.format = wgpu::TextureFormat::RGBA16Float;
	accumulationTextureDesc.mipLevelCount = 1;
	accumulationTextureDesc.sampleCount = 4;
	m_oitAccumulationTexture = m_device.CreateTexture(&accumulationTextureDesc);
	m_oitAccumulationView = m_oitAccumulationTexture.CreateView();

	wgpu::TextureDescriptor revealageTextureDesc{};
	revealageTextureDesc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
	revealageTextureDesc.dimension = wgpu::TextureDimension::e2D;
	revealageTextureDesc.size = { m_width, m_height };
	revealageTextureDesc.format = wgpu::TextureFormat::R8Unorm;
	revealageTextureDesc.mipLevelCount = 1;
	revealageTextureDesc.sampleCount = 4;
	m_oitRevealageTexture = m_device.CreateTexture(&revealageTextureDesc);
	m_oitRevealageView = m_oitRevealageTexture.CreateView();

	if (m_compositeGroupLayout)
		create_composite_bind_group();

	//
	// Setup Default Samplers
	//
	wgpu::SamplerDescriptor samplerNearestDesc
	{
		.addressModeU = wgpu::AddressMode::ClampToEdge,
		.addressModeV = wgpu::AddressMode::ClampToEdge,
		.addressModeW = wgpu::AddressMode::ClampToEdge,
		.magFilter = wgpu::FilterMode::Nearest,
		.minFilter = wgpu::FilterMode::Nearest,
		.mipmapFilter = wgpu::MipmapFilterMode::Nearest,
	};
	m_nearestSampler = m_device.CreateSampler(&samplerNearestDesc);

	wgpu::SamplerDescriptor samplerLinearDesc
	{
		.addressModeU = wgpu::AddressMode::ClampToEdge,
		.addressModeV = wgpu::AddressMode::ClampToEdge,
		.addressModeW = wgpu::AddressMode::ClampToEdge,
		.magFilter = wgpu::FilterMode::Linear,
		.minFilter = wgpu::FilterMode::Linear,
		.mipmapFilter = wgpu::MipmapFilterMode::Linear,
	};
	m_linearSampler = m_device.CreateSampler(&samplerLinearDesc);

	reload_pipeline();
	return true;
}

bool ax::Window::capture_next_frame(const std::filesystem::path& path)
{
	if (!m_surfaceSupportsCopySource || path.empty())
		return false;

	std::lock_guard lock{ m_frameImageRequestMutex };
	if (m_frameImageRequest.has_value())
		return false;

	m_frameImageRequest = FrameImageRequest{ .outputPath = path };
	return true;
}

bool ax::Window::compare_next_frame(Texture& reference, std::shared_ptr<Texture> referenceLease)
{
	if (!m_surfaceSupportsCopySource)
		return false;

	std::lock_guard lock{ m_frameImageRequestMutex };
	if (m_frameImageRequest.has_value())
		return false;

	m_frameImageRequest = FrameImageRequest{ .reference = &reference, .referenceLease = std::move(referenceLease) };
	return true;
}

ax::WindowFrameImageEvent ax::Window::read_frame_image
(
	const FrameImageRequest& request,
	wgpu::Buffer& readback,
	uint32_t bytesPerRow
)
{
	WindowFrameImageEvent result
	{
		.compared = request.reference != nullptr,
	};

	struct MapResult
	{
		std::atomic<bool> complete{ false };
		std::atomic<bool> succeeded{ false };
	};
	const auto mapResult{ std::make_shared<MapResult>() };
	const auto bufferSize{ static_cast<uint64_t>(bytesPerRow) * m_height };
	readback.MapAsync(wgpu::MapMode::Read, 0, bufferSize, wgpu::CallbackMode::AllowSpontaneous,
		[mapResult](wgpu::MapAsyncStatus status, wgpu::StringView)
		{
			mapResult->succeeded.store(status == wgpu::MapAsyncStatus::Success, std::memory_order_relaxed);
			mapResult->complete.store(true, std::memory_order_release);
		});

	const auto deadline{ std::chrono::steady_clock::now() + std::chrono::seconds{ 10 } };
	while (!mapResult->complete.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
	{
		m_device.Tick();
		std::this_thread::yield();
	}
	if (!mapResult->complete.load(std::memory_order_acquire))
	{
		result.message = "Timed out reading back the rendered frame.";
		return result;
	}
	if (!mapResult->succeeded.load(std::memory_order_relaxed))
	{
		result.message = "Failed to map the rendered frame readback buffer.";
		return result;
	}

	const auto* const mapped{ static_cast<const uint8_t*>(readback.GetConstMappedRange()) };
	if (mapped == nullptr)
	{
		readback.Unmap();
		result.message = "Failed to access the rendered frame readback data.";
		return result;
	}

	const auto rowBytes{ static_cast<size_t>(m_width) * 4 };
	std::vector<uint8_t> rgbaPixels{};
	rgbaPixels.resize(rowBytes * m_height);
	const bool bgraFormat{
		m_surfaceFormat == wgpu::TextureFormat::BGRA8Unorm
			|| m_surfaceFormat == wgpu::TextureFormat::BGRA8UnormSrgb
	};
	const bool rgbaFormat{
		m_surfaceFormat == wgpu::TextureFormat::RGBA8Unorm
			|| m_surfaceFormat == wgpu::TextureFormat::RGBA8UnormSrgb
	};
	if (!bgraFormat && !rgbaFormat)
	{
		readback.Unmap();
		result.message = "Unsupported surface format for frame image comparison.";
		return result;
	}

	for (uint32_t y{ 0 }; y < m_height; ++y)
	{
		const auto* const source{ mapped + static_cast<size_t>(y) * bytesPerRow };
		auto* const destination{ rgbaPixels.data() + static_cast<size_t>(y) * rowBytes };
		if (bgraFormat)
		{
			for (uint32_t x{ 0 }; x < m_width; ++x)
			{
				destination[x * 4] = source[x * 4 + 2];
				destination[x * 4 + 1] = source[x * 4 + 1];
				destination[x * 4 + 2] = source[x * 4];
				destination[x * 4 + 3] = source[x * 4 + 3];
			}
		}
		else
			std::memcpy(destination, source, rowBytes);
	}
	readback.Unmap();

	if (request.reference == nullptr)
	{
		const auto saveResult{ write_png(request.outputPath, m_width, m_height, 4, rgbaPixels.data(), static_cast<uint32_t>(rowBytes)) };
		result.success = saveResult == Error::Success;
		result.matches = result.success;
		if (!result.success)
			result.message = "Failed to save captured frame to '" + request.outputPath.string() + "'.";
		return result;
	}

	const auto& reference{ *request.reference };
	if (reference.m_width != m_width || reference.m_height != m_height)
	{
		result.message = std::format
		(
			"Reference image is {}x{}, but the rendered frame is {}x{}.",
			reference.m_width, reference.m_height, m_width, m_height
		);
		return result;
	}

	std::vector<uint8_t> gpuReferencePixels{};
	uint32_t referenceChannels{ reference.m_channels };
	const bool hasCpuPixels{ reference.m_stbiPtr != nullptr || !reference.m_pixels.empty() };
	const uint8_t* referencePixels{ reference.m_stbiPtr
		? static_cast<const uint8_t*>(reference.m_stbiPtr)
		: reference.m_pixels.data() };
	if (reference.m_gpuModified || !hasCpuPixels)
	{
		if (reference.m_device.Get() == nullptr || reference.m_texture.Get() == nullptr)
		{
			result.message = "Reference image has no readable GPU texture.";
			return result;
		}

		const uint64_t rowBytes{ static_cast<uint64_t>(reference.m_width) * 4 };
		const uint64_t paddedRowBytes{ (rowBytes + 255) & ~uint64_t{ 255 } };
		if (paddedRowBytes > std::numeric_limits<uint32_t>::max())
		{
			result.message = "Reference image row size exceeds WebGPU limits.";
			return result;
		}
		const uint64_t bufferSize{ paddedRowBytes * reference.m_height };

		wgpu::BufferDescriptor readbackDesc{};
		readbackDesc.size = bufferSize;
		readbackDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
		readbackDesc.label = "Frame comparison reference readback";
		auto referenceReadback{ reference.m_device.CreateBuffer(&readbackDesc) };

		wgpu::CommandEncoderDescriptor encoderDesc{};
		encoderDesc.label = "Frame comparison reference readback encoder";
		auto encoder{ reference.m_device.CreateCommandEncoder(&encoderDesc) };
		wgpu::TexelCopyTextureInfo source{};
		source.texture = reference.m_texture;
		wgpu::TexelCopyBufferInfo destination{};
		destination.buffer = referenceReadback;
		destination.layout.bytesPerRow = static_cast<uint32_t>(paddedRowBytes);
		destination.layout.rowsPerImage = reference.m_height;
		const wgpu::Extent3D extent{ reference.m_width, reference.m_height, 1 };
		encoder.CopyTextureToBuffer(&source, &destination, &extent);
		auto command{ encoder.Finish() };
		reference.m_device.GetQueue().Submit(1, &command);

		struct ReferenceMapResult
		{
			std::atomic<bool> complete{ false };
			std::atomic<bool> succeeded{ false };
		};
		const auto mapResult{ std::make_shared<ReferenceMapResult>() };
		referenceReadback.MapAsync(wgpu::MapMode::Read, 0, bufferSize, wgpu::CallbackMode::AllowSpontaneous,
			[mapResult](wgpu::MapAsyncStatus status, wgpu::StringView)
			{
				mapResult->succeeded.store(status == wgpu::MapAsyncStatus::Success, std::memory_order_relaxed);
				mapResult->complete.store(true, std::memory_order_release);
			});

		const auto deadline{ std::chrono::steady_clock::now() + std::chrono::seconds{ 10 } };
		while (!mapResult->complete.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
		{
			reference.m_device.Tick();
			std::this_thread::yield();
		}
		if (!mapResult->complete.load(std::memory_order_acquire))
		{
			result.message = "Timed out reading back the reference image.";
			return result;
		}
		if (!mapResult->succeeded.load(std::memory_order_relaxed))
		{
			result.message = "Failed to map the reference image readback buffer.";
			return result;
		}

		const auto* const mapped{ static_cast<const uint8_t*>(referenceReadback.GetConstMappedRange()) };
		if (mapped == nullptr)
		{
			referenceReadback.Unmap();
			result.message = "Failed to access the reference image readback data.";
			return result;
		}
		gpuReferencePixels.resize(static_cast<size_t>(rowBytes) * reference.m_height);
		for (uint32_t y{ 0 }; y < reference.m_height; ++y)
			std::memcpy(gpuReferencePixels.data() + static_cast<size_t>(y) * rowBytes,
				mapped + static_cast<size_t>(y) * paddedRowBytes, static_cast<size_t>(rowBytes));
		referenceReadback.Unmap();
		referencePixels = gpuReferencePixels.data();
		referenceChannels = 4;
	}

	if (referencePixels == nullptr || referenceChannels == 0 || referenceChannels > 4)
	{
		result.message = "Reference image has no readable pixel data.";
		return result;
	}

	const auto pixelCount{ static_cast<uint64_t>(m_width) * m_height };
	constexpr uint8_t s_channelTolerance{ 2 };
	uint64_t differingPixels{ 0 };
	uint32_t maxChannelDifference{ 0 };
	for (uint64_t i{ 0 }; i < pixelCount; ++i)
	{
		const auto* const source{ referencePixels + i * referenceChannels };
		const std::array<uint8_t, 4> expected
		{
			source[0],
			referenceChannels <= 2 ? source[0] : source[1],
			referenceChannels <= 2 ? source[0] : source[2],
			referenceChannels == 2 ? source[1] : referenceChannels == 4 ? source[3] : uint8_t{ 255 },
		};

		bool pixelDiffers{ false };
		for (uint32_t channel{ 0 }; channel < 4; ++channel)
		{
			const auto actual{ rgbaPixels[i * 4 + channel] };
			const auto difference{ static_cast<uint32_t>(std::abs(static_cast<int>(actual) - expected[channel])) };
			maxChannelDifference = std::max(maxChannelDifference, difference);
			pixelDiffers = pixelDiffers || difference > s_channelTolerance;
		}
		if (pixelDiffers)
			++differingPixels;
	}

	result.success = true;
	result.pixelCount = static_cast<uint32_t>(pixelCount);
	result.differingPixels = static_cast<uint32_t>(differingPixels);
	result.maxChannelDifference = maxChannelDifference;
	result.matches = differingPixels <= std::max<uint64_t>(1, pixelCount / 1000);
	if (!result.matches)
	{
		result.message = std::format
		(
			"{} of {} pixels differ by more than {} channel levels (maximum difference {}).",
			result.differingPixels, result.pixelCount, s_channelTolerance, result.maxChannelDifference
		);

		std::vector<uint8_t> encodedImage{};
		const auto encodeResult{ encode_png(m_width, m_height, 4, rgbaPixels.data(), static_cast<uint32_t>(rowBytes), encodedImage) };
		if (encodeResult == Error::Success)
			result.imageData.assign(reinterpret_cast<const char*>(encodedImage.data()), encodedImage.size());
		else
		{
			result.message += std::format(" Failed to encode the actual frame preview ({})", get_error_name(encodeResult));
			spdlog::error("Failed to encode the actual frame preview: {}", get_error_name(encodeResult));
		}
	}
	return result;
}

bool ax::Window::init_webgpu(wgpu::BackendType backendType)
{
	//
	// Get wgpu Instance
	//
	wgpu::InstanceDescriptor desc{};
	wgpu::Instance instance;
	desc.nextInChain = nullptr;

	static const auto s_timed_wait_any = wgpu::InstanceFeatureName::TimedWaitAny;
	// Dawn loads d3dcompiler_47.dll / vulkan-1.dll with LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR, which only
	// accepts absolute paths, so the system directory has to be given to it explicitly.
	char systemDir[MAX_PATH]{};
	const UINT systemDirLen{ ::GetSystemDirectoryA(systemDir, MAX_PATH) };
	std::string systemPath{ systemDir, systemDirLen };
	systemPath += '\\';
	const char* searchPaths[]{ systemPath.c_str() };

	dawn::native::DawnInstanceDescriptor dawnDesc{};
	dawnDesc.additionalRuntimeSearchPathsCount = 1;
	dawnDesc.additionalRuntimeSearchPaths = searchPaths;

	wgpu::InstanceDescriptor instanceDesc
	{
		.nextInChain = &dawnDesc,
		.requiredFeatureCount = 1,
		.requiredFeatures = &s_timed_wait_any
	};
	instance = wgpu::CreateInstance(&instanceDesc);
	
	//
	// Get Adapter
	//
	wgpu::RequestAdapterOptions options
	{
		.featureLevel = wgpu::FeatureLevel::Core,
		.backendType = backendType
	};

	auto adapter_callback =
		[](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message, void* userdata)
		{
			if (status != wgpu::RequestAdapterStatus::Success)
			{
				spdlog::error("Failed to get an adapter: {}", message.data);
				return;
			}
			*static_cast<wgpu::Adapter*>(userdata) = adapter;
		};

	const auto callbackMode{ wgpu::CallbackMode::WaitAnyOnly };
	void* userdata{ &m_adapter };
	instance.WaitAny(instance.RequestAdapter(&options, callbackMode, adapter_callback, userdata), UINT64_MAX);
	if (m_adapter == nullptr)
	{
		spdlog::error("RequestAdapter failed");
		return false;
	}

	//
	// Get Device
	//
	wgpu::Limits limits
	{
		.nextInChain = nullptr,
		.maxBindGroups = 2,
		.maxVertexBuffers = 1,
		.maxBufferSize = 150000 * sizeof(wgpu::VertexAttribute),
		.maxVertexAttributes = 4,
	};

	wgpu::DeviceDescriptor deviceDescriptor{};
	deviceDescriptor.requiredLimits = &limits;
	deviceDescriptor.SetUncapturedErrorCallback
	(
		[](const wgpu::Device&, wgpu::ErrorType error_type, wgpu::StringView message)
		{
			spdlog::error("Error: {} - message: {}", (uint32_t)error_type, message.data);
		}
	);
	deviceDescriptor.SetDeviceLostCallback
	(
		wgpu::CallbackMode::AllowProcessEvents,
		[](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message)
		{
			spdlog::error("Device Lost: {} - message: {}", (uint32_t)reason, message.data);
		}
	);

	auto device_callback =
		[](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message, void* userData)
		{
			if (status != wgpu::RequestDeviceStatus::Success)
			{
				spdlog::error("Failed to get a device: {}", message.data);
				return;
			}
			*static_cast<wgpu::Device*>(userData) = device;
		};

	instance.WaitAny(m_adapter.RequestDevice(&deviceDescriptor, callbackMode, device_callback, (void*)&m_device), UINT64_MAX);
	if (m_device == nullptr)
	{
		spdlog::error("RequestDevice failed");
		return false;
	}

	m_queue = m_device.GetQueue();

	//
	// Main Surface
	//
	m_surface = wgpu::Surface{ glfwGetWGPUSurface(instance.Get(), m_window) };

	return create_surfaces();
}

void ax::Window::reload_pipeline()
{
	LogTimer _timer{ "pipeline load" };

	// Load shader
	std::string shaderCode;
	std::ifstream file{ "shaders/basic_shader.wgsl" };
	if (file)
	{
		std::ostringstream stream;
		stream << file.rdbuf();
		shaderCode = stream.str();
	}
	else
	{
		spdlog::error("Could not load shader file, using fallback.");
		return;
	}

	wgpu::ShaderModuleDescriptor shaderDesc{};
	wgpu::ShaderSourceWGSL shaderCodeDesc;
	shaderCodeDesc.code = shaderCode.data();
	shaderDesc.nextInChain = &shaderCodeDesc;
	m_shader = m_device.CreateShaderModule(&shaderDesc);

	wgpu::RenderPipelineDescriptor pipelineDesc{};

	// Depth buffer
	wgpu::DepthStencilState depthStencilState = {};
	depthStencilState.format = m_depthTextureFormat;
	// Use reverse depth so higher z-index values are closer to the camera.
	depthStencilState.depthWriteEnabled = wgpu::OptionalBool::True;
	depthStencilState.depthCompare = wgpu::CompareFunction::GreaterEqual;
	depthStencilState.stencilFront.compare = wgpu::CompareFunction::Always;
	depthStencilState.stencilFront.failOp = wgpu::StencilOperation::Keep;
	depthStencilState.stencilFront.depthFailOp = wgpu::StencilOperation::Keep;
	depthStencilState.stencilFront.passOp = wgpu::StencilOperation::Keep;
	depthStencilState.stencilBack.compare = wgpu::CompareFunction::Always;
	depthStencilState.stencilBack.failOp = wgpu::StencilOperation::Keep;
	depthStencilState.stencilBack.depthFailOp = wgpu::StencilOperation::Keep;
	depthStencilState.stencilBack.passOp = wgpu::StencilOperation::Keep;
	pipelineDesc.depthStencil = &depthStencilState;

	// Vertex
	pipelineDesc.vertex.bufferCount = 0;
	pipelineDesc.vertex.buffers = nullptr;
	pipelineDesc.vertex.module = m_shader;
	pipelineDesc.vertex.entryPoint = "vs_main";
	pipelineDesc.vertex.constantCount = 0;
	pipelineDesc.vertex.constants = nullptr;

	// Topology
	pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
	pipelineDesc.primitive.stripIndexFormat = wgpu::IndexFormat::Undefined;
	pipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;
	pipelineDesc.primitive.cullMode = wgpu::CullMode::None;

	// Fragment Setup
	wgpu::FragmentState fragmentState;
	fragmentState.module = m_shader;
	fragmentState.entryPoint = "fs_opaque";
	fragmentState.constantCount = 0;
	fragmentState.constants = nullptr;
	wgpu::ColorTargetState colorTarget;
	colorTarget.format = m_surfaceFormat;
	colorTarget.blend = nullptr;
	colorTarget.writeMask = wgpu::ColorWriteMask::All;
	fragmentState.targetCount = 1;
	fragmentState.targets = &colorTarget;
	pipelineDesc.fragment = &fragmentState;

	// Multisampling (x4)
	pipelineDesc.multisample.count = 4;
	pipelineDesc.multisample.mask = ~0u;
	pipelineDesc.multisample.alphaToCoverageEnabled = false;

	// SpriteGpuData matches the WGSL instance layout.
	m_uniformStride = SpriteGpuData::gpuDataSize;
	wgpu::BufferDescriptor bufferDesc{};
	bufferDesc.size = static_cast<uint64_t>(m_uniformStride) * static_cast<uint64_t>(m_uniformsCapacity);
	bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Storage;
	bufferDesc.mappedAtCreation = false;
	bufferDesc.label = "SpriteBatch";
	m_uniforms = m_device.CreateBuffer(&bufferDesc);

	// Viewport
	wgpu::BufferDescriptor vpDesc{};
	vpDesc.size = 8 * sizeof(float);
	vpDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform;
	vpDesc.mappedAtCreation = false;
	vpDesc.label = "ViewportUBO";
	m_viewportBuffer = m_device.CreateBuffer(&vpDesc);

	auto vpEntries{ std::vector<wgpu::BindGroupEntry>{ 1 } };
	vpEntries[0].binding = 0;
	vpEntries[0].buffer = m_viewportBuffer;
	vpEntries[0].offset = 0;
	vpEntries[0].size = vpDesc.size;

	//
	// Bind Groups
	//

	auto bindGroupEntries{ std::vector<wgpu::BindGroupLayoutEntry>{ 3 } };

	// Uniforms
	bindGroupEntries[0].binding = 0;
	bindGroupEntries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
	bindGroupEntries[0].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
	bindGroupEntries[0].buffer.minBindingSize = static_cast<uint64_t>(m_uniformStride);
	bindGroupEntries[0].buffer.hasDynamicOffset = false;
	

	// Texture
	bindGroupEntries[1].binding = 1;
	bindGroupEntries[1].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
	bindGroupEntries[1].texture.sampleType = wgpu::TextureSampleType::Float;
	bindGroupEntries[1].texture.viewDimension = wgpu::TextureViewDimension::e2D;
	
	// Sampler
	bindGroupEntries[2].binding = 2;
	bindGroupEntries[2].visibility = wgpu::ShaderStage::Fragment;
	bindGroupEntries[2].sampler.type = wgpu::SamplerBindingType::Filtering;

	// Viewport
	auto globalEntries{ std::vector<wgpu::BindGroupLayoutEntry>{ 1 } };
	globalEntries[0].binding = 0;
	globalEntries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
	globalEntries[0].buffer.type = wgpu::BufferBindingType::Uniform;
	globalEntries[0].buffer.minBindingSize = static_cast<uint64_t>(8 * sizeof(float));

	//
	// Layout
	//
	
	// Group 0
	wgpu::BindGroupLayoutDescriptor layoutDesc{};
	layoutDesc.entryCount = bindGroupEntries.size();
	layoutDesc.entries = bindGroupEntries.data();
	m_groupLayout = m_device.CreateBindGroupLayout(&layoutDesc);

	// Group 1
	wgpu::BindGroupLayoutDescriptor globalLayoutDesc{};
	globalLayoutDesc.entryCount = globalEntries.size();
	globalLayoutDesc.entries = globalEntries.data();
	m_globalLayout = m_device.CreateBindGroupLayout(&globalLayoutDesc);

	// Bind em
	wgpu::BindGroupLayout layouts[2] = { m_groupLayout, m_globalLayout };
	wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{};
	pipelineLayoutDesc.bindGroupLayoutCount = 2;
	pipelineLayoutDesc.bindGroupLayouts = layouts;
	wgpu::PipelineLayout layout{ m_device.CreatePipelineLayout(&pipelineLayoutDesc) };

	wgpu::BindGroupDescriptor vpBindDesc{};
	vpBindDesc.layout = m_globalLayout;
	vpBindDesc.entryCount = vpEntries.size();
	vpBindDesc.entries = vpEntries.data();
	m_viewportBindGroup = m_device.CreateBindGroup(&vpBindDesc);

	//
	// Pipeline
	//
	pipelineDesc.layout = layout;
	m_pipeline = m_device.CreateRenderPipeline(&pipelineDesc);

	ShapeRenderer::init(m_device, {
		.colourFormat = m_surfaceFormat,
		.depthFormat = m_depthTextureFormat,
		.sampleCount = 4,
	});

	depthStencilState.depthWriteEnabled = wgpu::OptionalBool::False;
	depthStencilState.depthCompare = wgpu::CompareFunction::Greater;
	wgpu::BlendState accumulationBlend{};
	accumulationBlend.color.srcFactor = wgpu::BlendFactor::One;
	accumulationBlend.color.dstFactor = wgpu::BlendFactor::One;
	accumulationBlend.color.operation = wgpu::BlendOperation::Add;
	accumulationBlend.alpha.srcFactor = wgpu::BlendFactor::One;
	accumulationBlend.alpha.dstFactor = wgpu::BlendFactor::One;
	accumulationBlend.alpha.operation = wgpu::BlendOperation::Add;
	wgpu::BlendState revealageBlend{};
	revealageBlend.color.srcFactor = wgpu::BlendFactor::Zero;
	revealageBlend.color.dstFactor = wgpu::BlendFactor::OneMinusSrc;
	revealageBlend.color.operation = wgpu::BlendOperation::Add;
	wgpu::ColorTargetState oitTargets[2]{};
	oitTargets[0].format = wgpu::TextureFormat::RGBA16Float;
	oitTargets[0].blend = &accumulationBlend;
	oitTargets[0].writeMask = wgpu::ColorWriteMask::All;
	oitTargets[1].format = wgpu::TextureFormat::R8Unorm;
	oitTargets[1].blend = &revealageBlend;
	oitTargets[1].writeMask = wgpu::ColorWriteMask::All;
	fragmentState.entryPoint = "fs_oit";
	fragmentState.targetCount = 2;
	fragmentState.targets = oitTargets;
	m_oitPipeline = m_device.CreateRenderPipeline(&pipelineDesc);

	std::string compositeShaderCode;
	std::ifstream compositeFile{ "shaders/composite_shader.wgsl" };
	if (compositeFile)
	{
		std::ostringstream stream;
		stream << compositeFile.rdbuf();
		compositeShaderCode = stream.str();
	}
	else
	{
		spdlog::error("Could not load OIT composite shader file.");
		return;
	}
	wgpu::ShaderModuleDescriptor compositeShaderDesc{};
	wgpu::ShaderSourceWGSL compositeShaderCodeDesc;
	compositeShaderCodeDesc.code = compositeShaderCode.data();
	compositeShaderDesc.nextInChain = &compositeShaderCodeDesc;
	m_compositeShader = m_device.CreateShaderModule(&compositeShaderDesc);

	wgpu::BindGroupLayoutEntry compositeEntries[2]{};
	for (uint32_t i = 0; i < 2; ++i)
	{
		compositeEntries[i].binding = i;
		compositeEntries[i].visibility = wgpu::ShaderStage::Fragment;
		compositeEntries[i].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
		compositeEntries[i].texture.viewDimension = wgpu::TextureViewDimension::e2D;
		compositeEntries[i].texture.multisampled = true;
	}
	wgpu::BindGroupLayoutDescriptor compositeLayoutDesc{};
	compositeLayoutDesc.entryCount = 2;
	compositeLayoutDesc.entries = compositeEntries;
	m_compositeGroupLayout = m_device.CreateBindGroupLayout(&compositeLayoutDesc);
	create_composite_bind_group();

	wgpu::PipelineLayoutDescriptor compositePipelineLayoutDesc{};
	compositePipelineLayoutDesc.bindGroupLayoutCount = 1;
	compositePipelineLayoutDesc.bindGroupLayouts = &m_compositeGroupLayout;
	wgpu::PipelineLayout compositePipelineLayout = m_device.CreatePipelineLayout(&compositePipelineLayoutDesc);
	wgpu::RenderPipelineDescriptor compositePipelineDesc{};
	compositePipelineDesc.layout = compositePipelineLayout;
	compositePipelineDesc.vertex.module = m_compositeShader;
	compositePipelineDesc.vertex.entryPoint = "vs_main";
	compositePipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
	compositePipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;
	compositePipelineDesc.primitive.cullMode = wgpu::CullMode::None;
	compositePipelineDesc.multisample.count = 4;
	compositePipelineDesc.multisample.mask = ~0u;
	wgpu::BlendState compositeBlend{};
	compositeBlend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
	compositeBlend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
	compositeBlend.color.operation = wgpu::BlendOperation::Add;
	compositeBlend.alpha.srcFactor = wgpu::BlendFactor::One;
	compositeBlend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
	compositeBlend.alpha.operation = wgpu::BlendOperation::Add;
	wgpu::ColorTargetState compositeTarget{};
	compositeTarget.format = m_surfaceFormat;
	compositeTarget.blend = &compositeBlend;
	compositeTarget.writeMask = wgpu::ColorWriteMask::All;
	wgpu::FragmentState compositeFragment{};
	compositeFragment.module = m_compositeShader;
	compositeFragment.entryPoint = "fs_main";
	compositeFragment.targetCount = 1;
	compositeFragment.targets = &compositeTarget;
	compositePipelineDesc.fragment = &compositeFragment;
	m_compositePipeline = m_device.CreateRenderPipeline(&compositePipelineDesc);

	{
		std::lock_guard lock{ m_textureBindGroupsMutex };
		m_textureBindGroups.clear();
	}
	m_pendingPipelines.clear();
}

void ax::Window::create_composite_bind_group()
{
	wgpu::BindGroupEntry entries[2]{};
	entries[0].binding = 0;
	entries[0].textureView = m_oitAccumulationView;
	entries[1].binding = 1;
	entries[1].textureView = m_oitRevealageView;
	wgpu::BindGroupDescriptor bindGroupDesc{};
	bindGroupDesc.layout = m_compositeGroupLayout;
	bindGroupDesc.entryCount = 2;
	bindGroupDesc.entries = entries;
	m_compositeBindGroup = m_device.CreateBindGroup(&bindGroupDesc);
}

ax::SpriteDefinition* ax::Window::allocate_sprite()
{
	SpriteDefinition* sprite;
	if (m_freeSpriteSlots.empty())
	{
		m_spriteSlots.emplace_back();
		sprite = &m_spriteSlots.back();
	}
	else
	{
		sprite = m_freeSpriteSlots.back();
		m_freeSpriteSlots.pop_back();
		*sprite = {};
	}

	m_activeSprites.push_back(sprite);
	++m_spriteAllocations;
	sprite->activeIndex = m_activeSprites.size() - 1;
	sprite->allocated = true;
	sprite->generation = ++m_spriteGeneration;
	return sprite;
}

std::shared_ptr<ax::SpriteGroup> ax::Window::create_sprite_group()
{
	if (m_rendererThread != std::this_thread::get_id())
		throw sol::error("Sprite groups must be created on the renderer thread");
	auto group{ std::make_shared<SpriteGroup>() };
	group->m_owner = this;
	m_spriteVisibilityGroups.push_back(group);
	return group;
}

ax::SpriteDefinition* ax::Window::allocate_group_sprite(const std::shared_ptr<SpriteGroup>& group)
{
	if (!group || group->m_released || group->m_owner != this)
		throw sol::error("Invalid sprite group");
	group->check_thread();
	auto sprite{ allocate_sprite() };
	transfer_sprite(sprite, group);
	return sprite;
}

void ax::Window::transfer_sprite(SpriteDefinition* sprite, const std::shared_ptr<SpriteGroup>& group)
{
	if (!sprite || !sprite->allocated || !group || group->m_released || group->m_owner != this)
		throw sol::error("Invalid sprite or sprite group");
	group->check_thread();
	if (sprite->group == group.get())
		return;
	if (sprite->group)
	{
		auto& old{ sprite->group->m_sprites };
		old.back()->groupIndex = sprite->groupIndex;
		old[sprite->groupIndex] = old.back();
		old.pop_back();
	}
	sprite->group = group.get();
	sprite->groupIndex = group->m_sprites.size();
	group->m_sprites.push_back(sprite);
}

void ax::Window::release_sprite_group(const std::shared_ptr<SpriteGroup>& group)
{
	if (!group || group->m_owner != this)
		throw sol::error("Invalid sprite group");
	group->check_thread();
	if (group->m_released)
		return;
	while (!group->m_sprites.empty())
		free_sprite(group->m_sprites.back());
	group->m_released = true;
	group->m_visible = false;
	std::erase(m_spriteVisibilityGroups, group);
}

std::shared_ptr<ax::StaticSpriteBatch> ax::Window::attach_sprite_batch(Texture* texture,
	std::shared_ptr<SpriteBuffer> data, glm::vec2 atlasOffset, std::int64_t order)
{
	if (m_rendererThread != std::this_thread::get_id())
		throw sol::error("Static batches must be attached on the renderer thread");
	if (!texture || !data || !data->sealed() || data->size() * SpriteGpuData::gpuDataSize > 65536)
		throw sol::error("Static sprite batches require a texture and 1-1024 sealed records");
	auto batch{ std::make_shared<StaticSpriteBatch>() };
	batch->m_texture = texture;
	batch->m_data = std::move(data);
	batch->m_atlasOffset = atlasOffset;
	batch->m_order = order;
	m_staticSpriteBatches.push_back(batch);
	std::stable_sort(m_staticSpriteBatches.begin(), m_staticSpriteBatches.end(),
		[](const auto& a, const auto& b) { return a->m_order < b->m_order; });
	return batch;
}

void ax::Window::stage_sprite_batches()
{
	std::erase_if(m_staticSpriteBatches, [](const auto& batch) { return batch->released(); });
	if (const auto batch{ next_static_sprite_batch(m_staticSpriteBatches) })
	{
		try
		{
			auto buffer{ upload_static_sprites(m_device, *batch->m_data, batch->m_atlasOffset) };
			batch->m_bindGroup = setup_bind_groups(batch->m_texture->view(), buffer);
			batch->m_buffer = std::move(buffer);
			++m_staticUploads;
		}
		catch (const std::exception& error)
		{
			batch->m_error = error.what();
			spdlog::error("Static sprite batch staging failed: {}", batch->m_error);
		}
	}
	m_spriteSetupDeadline = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() + 0.001;
}

void ax::Window::free_sprite(SpriteDefinition* sprite)
{
	if (!sprite || !sprite->allocated)
		return;
	const auto index{ sprite->activeIndex };
	if (sprite->group)
	{
		auto& members{ sprite->group->m_sprites };
		members.back()->groupIndex = sprite->groupIndex;
		members[sprite->groupIndex] = members.back();
		members.pop_back();
	}

	if (sprite->groupedTexture != nullptr)
	{
		auto group = m_spriteGroups.find(sprite->groupedTexture);
		if (group != m_spriteGroups.end())
			group->second.erase(std::remove(group->second.begin(), group->second.end(), sprite), group->second.end());
	}

	m_activeSprites.back()->activeIndex = index;
	m_activeSprites[index] = m_activeSprites.back();
	m_activeSprites.pop_back();
	*sprite = {};
	m_freeSpriteSlots.push_back(sprite);
	++m_spriteFrees;
}

std::size_t ax::Window::static_batch_count() const
{
	return std::count_if(m_staticSpriteBatches.begin(), m_staticSpriteBatches.end(),
		[](const auto& batch) { return !batch->released(); });
}

std::size_t ax::Window::get_sprite_count() const
{
	return m_activeSprites.size();
}

void ax::Window::invalidate_texture_bind_group(Texture* texture)
{
	if (texture == nullptr)
		return;

	std::lock_guard lock{ m_textureBindGroupsMutex };
	m_textureBindGroups.erase(texture);
}

bool ax::Window::submit_pending_draw_batch(PendingDrawBatch batch)
{
	if (batch.target != nullptr && (batch.target->texture().Get() == nullptr || batch.target->view().Get() == nullptr
		|| batch.target->width() == 0 || batch.target->height() == 0))
	{
		spdlog::error("Cannot submit pending draws to an invalid render texture.");
		return false;
	}

	if (batch.shapes.empty() && batch.textures.empty())
		return true;

	std::lock_guard lock{ m_pendingDrawMutex };
	m_pendingDrawBatches.push_back(std::move(batch));
	return true;
}

void ax::Window::draw_pending()
{
	PendingDrawBatch batch{};
	batch.shapes = DebugShapes::take_queued();
	{
		std::lock_guard lock{ m_pendingTextureMutex };
		batch.textures = std::move(m_queuedTextures);
		m_queuedTextures.clear();
	}
	submit_pending_draw_batch(std::move(batch));
}

wgpu::RenderPipeline ax::Window::setup_pending_pipeline(wgpu::TextureFormat format)
{
	const auto found{ m_pendingPipelines.find(format) };
	if (found != m_pendingPipelines.end())
		return found->second;

	wgpu::BindGroupLayout layouts[2]{ m_groupLayout, m_globalLayout };
	wgpu::PipelineLayoutDescriptor layoutDesc{};
	layoutDesc.bindGroupLayoutCount = 2;
	layoutDesc.bindGroupLayouts = layouts;
	const auto layout{ m_device.CreatePipelineLayout(&layoutDesc) };

	wgpu::BlendState blend{};
	blend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
	blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
	blend.color.operation = wgpu::BlendOperation::Add;
	blend.alpha.srcFactor = wgpu::BlendFactor::One;
	blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
	blend.alpha.operation = wgpu::BlendOperation::Add;

	wgpu::ColorTargetState target{};
	target.format = format;
	target.blend = &blend;
	target.writeMask = wgpu::ColorWriteMask::All;
	wgpu::FragmentState fragment{};
	fragment.module = m_shader;
	fragment.entryPoint = "fs_main";
	fragment.targetCount = 1;
	fragment.targets = &target;

	wgpu::RenderPipelineDescriptor desc{};
	desc.layout = layout;
	desc.vertex.module = m_shader;
	desc.vertex.entryPoint = "vs_main";
	desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
	desc.primitive.frontFace = wgpu::FrontFace::CCW;
	desc.primitive.cullMode = wgpu::CullMode::None;
	desc.multisample.count = 1;
	desc.multisample.mask = ~0u;
	desc.fragment = &fragment;

	auto pipeline{ m_device.CreateRenderPipeline(&desc) };
	if (pipeline.Get() == nullptr)
	{
		spdlog::error("Failed to create a pending-draw pipeline for texture format {}.", static_cast<uint32_t>(format));
		return {};
	}

	m_pendingPipelines.emplace(format, pipeline);
	return pipeline;
}

void ax::Window::draw_pending_batch(const PendingDrawBatch& batch, const wgpu::TextureView& screenView)
{
	const auto targetView{ batch.target != nullptr ? batch.target->view() : screenView };
	const auto format{ batch.target != nullptr ? batch.target->texture().GetFormat() : m_surfaceFormat };
	const auto width{ batch.target != nullptr ? batch.target->width() : m_width };
	const auto height{ batch.target != nullptr ? batch.target->height() : m_height };

	std::vector<const SpriteDefinition*> sprites{};
	sprites.reserve(batch.textures.size());
	for (const auto& sprite : batch.textures)
	{
		if (sprite.tex == nullptr)
			continue;
		if (sprite.tex == batch.target)
		{
			spdlog::error("Cannot sample a texture while drawing pending content into that same texture.");
			continue;
		}
		sprites.push_back(&sprite);
	}

	if (batch.shapes.empty() && sprites.empty())
		return;

	const auto pipeline{ sprites.empty() ? wgpu::RenderPipeline{} : setup_pending_pipeline(format) };
	if (!sprites.empty() && pipeline.Get() == nullptr)
		return;

	if (!ShapeRenderer::init(m_device, {
		.colourFormat = format,
		.depthFormat = wgpu::TextureFormat::Undefined,
		.sampleCount = 1,
	}))
		return;

	const ShapeView shapeView
	{
		.viewport = { static_cast<float>(width), static_cast<float>(height) },
		.cameraPosition = m_camera.position(),
		.zoom = m_camera.zoom(),
	};
	ShapeRenderer::begin_frame(shapeView);

	const float vp[8]{
		static_cast<float>(width), static_cast<float>(height), 0.f, 0.f,
		m_camera.position().x, m_camera.position().y, m_camera.zoom(), 0.f,
	};
	m_queue.WriteBuffer(m_viewportBuffer, 0, vp, sizeof(vp));

	wgpu::CommandEncoderDescriptor encoderDesc{};
	encoderDesc.label = "pending draw encoder";
	auto encoder{ m_device.CreateCommandEncoder(&encoderDesc) };
	wgpu::RenderPassColorAttachment colorAttachment{};
	colorAttachment.view = targetView;
	colorAttachment.depthSlice = wgpu::kDepthSliceUndefined;
	colorAttachment.loadOp = wgpu::LoadOp::Load;
	colorAttachment.storeOp = wgpu::StoreOp::Store;
	wgpu::RenderPassDescriptor passDesc{};
	passDesc.colorAttachmentCount = 1;
	passDesc.colorAttachments = &colorAttachment;
	auto pass{ encoder.BeginRenderPass(&passDesc) };

	if (!sprites.empty())
	{
		ensure_uniform_capacity(static_cast<uint32_t>(sprites.size()));
		m_spriteUploadData.clear();
		m_spriteUploadData.reserve(sprites.size());
		for (const auto* sprite : sprites)
			m_spriteUploadData.push_back(sprite->gpuData);
		m_queue.WriteBuffer(m_uniforms, 0, m_spriteUploadData.data(), m_spriteUploadData.size() * m_uniformStride);

		pass.SetPipeline(pipeline);
		pass.SetBindGroup(1, m_viewportBindGroup);
		for (uint32_t first = 0; first < sprites.size();)
		{
			uint32_t end{ first + 1 };
			while (end < sprites.size() && sprites[end]->tex == sprites[first]->tex)
				++end;

			pass.SetBindGroup(0, get_texture_bind_group(sprites[first]->tex));
			pass.Draw(6, end - first, 0, first);
			first = end;
		}
	}

	if (!batch.shapes.empty())
		ShapeRenderer::draw(pass, batch.shapes);

	pass.End();
	wgpu::CommandBufferDescriptor commandDesc{};
	commandDesc.label = "pending draw command buffer";
	auto command{ encoder.Finish(&commandDesc) };
	m_queue.Submit(1, &command);
	if (batch.target != nullptr)
		batch.target->m_gpuModified = true;
}

void ax::Window::queue_texture(SpriteDefinition sprite)
{
	std::lock_guard lock{ m_pendingTextureMutex };
	m_queuedTextures.push_back(std::move(sprite));
}

void ax::Window::render_texture(Texture* tex, glm::vec2 position, std::shared_ptr<Texture> keepAlive)
{
	SpriteDefinition sprite{};
	sprite.tex = tex;
	sprite.gpuData.pos = position;
	sprite.keepAlive = std::move(keepAlive);
	queue_texture(std::move(sprite));
}

void ax::Window::render_texture(Texture* tex, glm::vec2 position, rectf region, std::shared_ptr<Texture> keepAlive)
{
	SpriteDefinition sprite{};
	sprite.tex = tex;
	sprite.gpuData.pos = position;
	sprite.gpuData.region = region;
	sprite.gpuData.useRegion = 1;
	sprite.keepAlive = std::move(keepAlive);
	queue_texture(std::move(sprite));
}

void ax::Window::render_texture(Texture* tex, glm::vec2 position, float r, rectf region, float z, glm::vec4 color, glm::vec2 scale, std::shared_ptr<Texture> keepAlive)
{
	SpriteDefinition sprite{};
	sprite.tex = tex;
	sprite.gpuData.pos = position;
	sprite.gpuData.rotation = r;
	sprite.gpuData.region = region;
	sprite.gpuData.useRegion = 1;
	sprite.gpuData.z = z;
	sprite.gpuData.tint = color;
	sprite.gpuData.scale = scale;
	sprite.keepAlive = std::move(keepAlive);
	queue_texture(std::move(sprite));
}

void ax::Window::render_texture(Texture* tex, glm::vec2 position, float z, std::shared_ptr<Texture> keepAlive)
{
	SpriteDefinition sprite{};
	sprite.tex = tex;
	sprite.gpuData.pos = position;
	sprite.gpuData.z = z;
	sprite.keepAlive = std::move(keepAlive);
	queue_texture(std::move(sprite));
}

void ax::Window::render_texture(Texture* tex, glm::vec2 position, rectf region, float z, std::shared_ptr<Texture> keepAlive)
{
	SpriteDefinition sprite{};
	sprite.tex = tex;
	sprite.gpuData.pos = position;
	sprite.gpuData.region = region;
	sprite.gpuData.useRegion = 1;
	sprite.gpuData.z = z;
	sprite.keepAlive = std::move(keepAlive);
	queue_texture(std::move(sprite));
}

void ax::Window::render_ui_texture(Texture* tex, glm::vec2 position, std::shared_ptr<Texture> keepAlive)
{
	SpriteDefinition sprite{};
	sprite.tex = tex;
	sprite.gpuData.pos = position;
	sprite.gpuData.screenSpace = 1;
	sprite.keepAlive = std::move(keepAlive);
	queue_texture(std::move(sprite));
}

void ax::Window::render_ui_texture(Texture* tex, glm::vec2 position, rectf region, std::shared_ptr<Texture> keepAlive)
{
	SpriteDefinition sprite{};
	sprite.tex = tex;
	sprite.gpuData.pos = position;
	sprite.gpuData.region = region;
	sprite.gpuData.useRegion = 1;
	sprite.gpuData.screenSpace = 1;
	sprite.keepAlive = std::move(keepAlive);
	queue_texture(std::move(sprite));
}

void ax::Window::render_ui_texture(Texture* tex, glm::vec2 position, float r, rectf region, float z, glm::vec4 color, glm::vec2 scale, std::shared_ptr<Texture> keepAlive)
{
	SpriteDefinition sprite{};
	sprite.tex = tex;
	sprite.gpuData.pos = position;
	sprite.gpuData.rotation = r;
	sprite.gpuData.region = region;
	sprite.gpuData.useRegion = 1;
	sprite.gpuData.z = z;
	sprite.gpuData.tint = color;
	sprite.gpuData.scale = scale;
	sprite.gpuData.screenSpace = 1;
	sprite.keepAlive = std::move(keepAlive);
	queue_texture(std::move(sprite));
}

void ax::Window::render_ui_texture(Texture* tex, glm::vec2 position, float z, std::shared_ptr<Texture> keepAlive)
{
	SpriteDefinition sprite{};
	sprite.tex = tex;
	sprite.gpuData.pos = position;
	sprite.gpuData.z = z;
	sprite.gpuData.screenSpace = 1;
	sprite.keepAlive = std::move(keepAlive);
	queue_texture(std::move(sprite));
}

void ax::Window::render_ui_texture(Texture* tex, glm::vec2 position, rectf region, float z, std::shared_ptr<Texture> keepAlive)
{
	SpriteDefinition sprite{};
	sprite.tex = tex;
	sprite.gpuData.pos = position;
	sprite.gpuData.region = region;
	sprite.gpuData.useRegion = 1;
	sprite.gpuData.z = z;
	sprite.gpuData.screenSpace = 1;
	sprite.keepAlive = std::move(keepAlive);
	queue_texture(std::move(sprite));
}

void ax::Window::call_deferred(std::function<void()> func)
{
	std::lock_guard lock{ m_deferredMutex };
	m_deferred.push_back(std::move(func));
}

#include "glfw/glfw3native.h"
bool ax::Window::try_embed_child(DWORD procid)
{
	const auto my_hwnd{ glfwGetWin32Window(m_window) };

	struct EnumWindowContext
	{
		DWORD process_id;
		HWND child_handle;
	} context{ procid, nullptr };

	::EnumWindows
	(
		[](HWND hwnd, LPARAM lParam) -> BOOL
		{
			auto* context{ reinterpret_cast<EnumWindowContext*>(lParam) };
			DWORD windowProcId;

			if (!::IsWindowVisible(hwnd))
				return TRUE;

			::GetWindowThreadProcessId(hwnd, &windowProcId);
			if (windowProcId == context->process_id)
			{
				context->child_handle = hwnd;
				return FALSE;
			}

			return TRUE;
		},
		reinterpret_cast<LPARAM>(&context)
	);

	if (context.child_handle != nullptr)
	{
		::SetParent(context.child_handle, my_hwnd);
		LONG style = ::GetWindowLong(context.child_handle, GWL_STYLE);
		style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
		::SetWindowLong(context.child_handle, GWL_STYLE, style | WS_CHILD);
		::SetWindowPos(context.child_handle, NULL, 0, 0, 0, 0, SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOOWNERZORDER);

		m_childWindows[context.process_id] = context.child_handle;

		return true;
	}

	return false;
}

void ax::Window::try_kill_child(DWORD processID)
{
	auto it = m_childWindows.find(processID);
	if (it != m_childWindows.end())
	{
		HWND childHandle = it->second;
		if (::IsWindow(childHandle))
		{
			::SendMessage(childHandle, WM_CLOSE, 0, 0);
			::SetParent(childHandle, NULL);
		}
		m_childWindows.erase(it);
	}
}

void ax::Window::set_embedded_child_position(DWORD processID, int x, int y, int width, int height)
{
	auto it = m_childWindows.find(processID);
	if (it != m_childWindows.end())
	{
		HWND childHandle = it->second;

		if (!(::IsWindow(childHandle)))
			m_childWindows.erase(it);
		else
			::SetWindowPos(childHandle, NULL, x, y, width, height, SWP_SHOWWINDOW | SWP_NOZORDER | SWP_NOOWNERZORDER);
	}
}

void ax::Window::focus_child(DWORD processID)
{
	auto it = m_childWindows.find(processID);
	if (it != m_childWindows.end())
	{
		HWND childHandle = it->second;
		if (!(::IsWindow(childHandle)))
			m_childWindows.erase(it);
		else
			::SetFocus(childHandle);
	}
}

static std::mutex s_redirectMutex{};
static std::map<HWND, WNDPROC> s_originalWndProc{};
static std::map<HWND, HWND> s_redirectHandles{};

void ax::Window::redirect_input_to_child(DWORD processID)
{
	auto it = m_childWindows.find(processID);
	if (it != m_childWindows.end())
	{
		HWND childHandle = it->second;
		if (!(::IsWindow(childHandle)))
			m_childWindows.erase(it);
		else
		{
			std::lock_guard lock{ s_redirectMutex };

			HWND hWnd = ::glfwGetWin32Window(m_window);

			ImGui_ImplGlfw_RestoreCallbacks(m_window);

			if (s_originalWndProc.find(hWnd) == s_originalWndProc.end())
				s_originalWndProc[hWnd] = (WNDPROC)::GetWindowLongPtr(hWnd, GWLP_WNDPROC);

			s_redirectHandles[hWnd] = childHandle;
			::SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)ax::Window::raw_windows_event);

			register_window_events();
			ImGui_ImplGlfw_InstallCallbacks(m_window);
		}
	}
}


LRESULT ax::Window::raw_windows_event(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_KEYDOWN:
	case WM_KEYUP:
	{
		::SendMessage(s_redirectHandles[hwnd], msg, wParam, lParam);
		return 0;
	}
	default:
		break;
	}

	return ::CallWindowProc(s_originalWndProc[hwnd], hwnd, msg, wParam, lParam);
}


void ax::Window::reset_input_redirection()
{
	std::lock_guard lock{ s_redirectMutex };

	const auto hwnd{ ::glfwGetWin32Window(m_window) };
	const auto originalWndProc{ s_originalWndProc.find(hwnd) };
	if (originalWndProc == s_originalWndProc.end())
		return;

	ImGui_ImplGlfw_RestoreCallbacks(m_window);
	s_redirectHandles.erase(hwnd);
	::SetWindowLongPtr(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalWndProc->second));
	s_originalWndProc.erase(originalWndProc);

	register_window_events();
	ImGui_ImplGlfw_InstallCallbacks(m_window);
}

void ax::Window::ensure_uniform_capacity(uint32_t required)
{
	if (required <= m_uniformsCapacity)
		return;

	while (m_uniformsCapacity < required)
		m_uniformsCapacity *= 2;

	wgpu::BufferDescriptor bufferDesc{};
	bufferDesc.size = static_cast<uint64_t>(m_uniformStride) * static_cast<uint64_t>(m_uniformsCapacity);
	bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Storage;
	bufferDesc.mappedAtCreation = false;
	bufferDesc.label = "SpriteBatch";
	m_uniforms = m_device.CreateBuffer(&bufferDesc);
	{
		std::lock_guard lock{ m_textureBindGroupsMutex };
		m_textureBindGroups.clear();
	}
}

glm::vec2 ax::Window::global_to_viewport(const glm::vec2& position) const
{
	const glm::vec2 viewportCenter{ m_width * 0.5f, m_height * 0.5f };
	return ((position - viewportCenter) / m_camera.zoom()) + m_camera.position();
}

glm::vec4 ax::Window::global_to_viewport(const glm::vec4& rect) const
{
	return glm::vec4(global_to_viewport({ rect.x, rect.y }), rect.z / m_camera.zoom(), rect.w / m_camera.zoom());
}

glm::vec2 ax::Window::viewport_to_global(const glm::vec2& position) const
{
	const glm::vec2 viewportCenter{ m_width * 0.5f, m_height * 0.5f };
	return ((position - m_camera.position()) * m_camera.zoom()) + viewportCenter;
}

glm::vec4 ax::Window::viewport_to_global(const glm::vec4& rect) const
{
	return glm::vec4(viewport_to_global({ rect.x, rect.y }), rect.z * m_camera.zoom(), rect.w * m_camera.zoom());
}

bool ax::Window::screen_contains_region(const glm::vec4& rect) const
{
	const auto transformedRect { viewport_to_global(rect) };
	return transformedRect.x + transformedRect.z >= 0
			&& transformedRect.y + transformedRect.w >= 0
			&& transformedRect.x <= m_width
			&& transformedRect.y <= m_height;
}

wgpu::BindGroup ax::Window::setup_bind_groups(const wgpu::TextureView& view)
{
	return setup_bind_groups(view, m_uniforms);
}

wgpu::BindGroup ax::Window::setup_bind_groups(const wgpu::TextureView& view, const wgpu::Buffer& buffer)
{
	auto bindGroups{ std::vector<wgpu::BindGroupEntry>{ 3 } };

	// Uniforms
	bindGroups[0].binding = 0;
	bindGroups[0].buffer = buffer;
	bindGroups[0].offset = 0;
	bindGroups[0].size = buffer.GetSize();

	// Texture
	bindGroups[1].binding = 1;
	bindGroups[1].textureView = view;

	// Sampler
	bindGroups[2].binding = 2;
	bindGroups[2].sampler = m_linearSampler;

	wgpu::BindGroupDescriptor bindGroupDesc{};
	bindGroupDesc.layout = m_groupLayout;
	bindGroupDesc.entryCount = bindGroups.size();
	bindGroupDesc.entries = bindGroups.data();
	wgpu::BindGroup binds = m_device.CreateBindGroup(&bindGroupDesc);
	return binds;
}

wgpu::BindGroup ax::Window::get_texture_bind_group(Texture* texture)
{
	if (texture == nullptr)
		return {};

	std::lock_guard lock{ m_textureBindGroupsMutex };
	if (const auto it{ m_textureBindGroups.find(texture) }; it != m_textureBindGroups.end())
		return it->second;

	auto bindGroup{ setup_bind_groups(texture->view()) };
	m_textureBindGroups.emplace(texture, bindGroup);
	return bindGroup;
}

void ax::Window::handle_tick(double delta)
{
	m_updateEventHandler.fire({ .delta = delta });
}

void ax::Window::prevent_close()
{
	m_can_close = false;
}

bool ax::Window::init_imgui()
{
	LogTimer _timer{ "imgui setup" };

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::GetIO();

	ImGui_ImplGlfw_InitForOther(m_window, false);

	ImGui_ImplWGPU_InitInfo info{};
	info.Device = m_device.Get();
	info.DepthStencilFormat = static_cast<WGPUTextureFormat>(m_depthTextureFormat);
	info.RenderTargetFormat = static_cast<WGPUTextureFormat>(m_surfaceFormat);
	info.PipelineMultisampleState.count = 4;
	ImGui_ImplWGPU_Init(&info);

	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	ax::setup_imgui_style();
	s_imgui_initialized = true;

	// Default font
	ImGuiIO& io = ImGui::GetIO();
	ImFontConfig default_font_config{};
	const auto baseFontSize = 15.0f;
	default_font_config.SizePixels = baseFontSize;
	default_font_config.ExtraSizeScale = 1.0f;
	io.Fonts->AddFontFromFileTTF("fonts/Montserrat-Medium.ttf", baseFontSize, &default_font_config);

	// Merge in icons from Font Awesome
	const auto iconFontSize = baseFontSize * 2.0f / 3.0f;
	static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
	ImFontConfig icons_config{};
	icons_config.MergeMode = true;
	icons_config.PixelSnapH = true;
	icons_config.GlyphMinAdvanceX = iconFontSize;
	icons_config.SizePixels = iconFontSize;
	icons_config.ExtraSizeScale = 1.0f;
	const auto merged_font{ io.Fonts->AddFontFromFileTTF("fonts/" FONT_ICON_FILE_NAME_FAS, iconFontSize, &icons_config, icons_ranges) };
	ax::ImGuiHelper::add_font("default", merged_font);

	// Add a monospace font
	ImFontConfig mono_font_config{};
	mono_font_config.SizePixels = baseFontSize;
	mono_font_config.ExtraSizeScale = 1.0f;
	const auto mono_font{ io.Fonts->AddFontFromFileTTF("fonts/SourceCodePro-Medium.ttf", baseFontSize, &mono_font_config) };
	ax::ImGuiHelper::add_font("mono", mono_font);

	return true;
}

void ax::Window::register_window_events()
{
	glfwSetWindowSizeCallback(m_window, &resize_event_handler);
	glfwSetWindowCloseCallback(m_window, ax::Window::window_close_handler);

	ax::input::KeyEventHandler::register_events(m_window);
	ax::input::MouseButtonEventHandler::register_events(m_window);
	ax::input::MouseMoveEventHandler::register_events(m_window);
	ax::input::MouseScrollEventHandler::register_events(m_window);
}

void ax::Window::window_close_handler(GLFWwindow* window)
{
	const auto idx{ s_windows.find(window) };
	if (idx != s_windows.end())
	{
		if (idx->second != nullptr)
		{
			// Set m_can_close to false here (Window::prevent_close()) to stop window closing.
			idx->second->get_request_close_event_handler().fire({ idx->second });

			if (!idx->second->m_can_close)
			{
				glfwSetWindowShouldClose(window, false);
				idx->second->m_can_close = true;
			}
		}
	}
}

double updatePrev{ 0 };
double updateDelta{ 0 };
void ax::Window::run_loop()
{
	register_window_events();
	ImGui_ImplGlfw_InstallCallbacks(m_window);

	//SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

	while (!glfwWindowShouldClose(m_window))
	{
		glfwPollEvents();
		stage_sprite_batches();

		updateDelta = glfwGetTime() - updatePrev;
		updatePrev = glfwGetTime();

		auto surfaceFuture = 
			std::async(std::launch::async, 
				[this]()
				{
					// Get current render surface
					wgpu::SurfaceTexture surfaceTexture;
					m_surface.GetCurrentTexture(&surfaceTexture);
					if (surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal)
					{
						spdlog::error("Failed to get new frame surface");
						return wgpu::SurfaceTexture{};
					}
					return surfaceTexture;
				}
			);

		PROFILER_TOP_LEVEL(frame);
		PROFILER_TOP_LEVEL_SEGMENT_SCOPED(frame);
		{
			PROFILER_SEGMENT_SCOPED(frame, tick);
			handle_tick(updateDelta);
		}

		{
			PROFILER_SEGMENT_SCOPED(frame, render);
			run_wgpu_render_pass(updateDelta, std::move(surfaceFuture));
		}

		{
			PROFILER_SEGMENT_SCOPED(frame, deferred);
			std::vector<std::function<void()>> deferred;
			{
				std::lock_guard lock{ m_deferredMutex };
				deferred.swap(m_deferred);
			}
			for (auto& func : deferred)
				func();
		}
	}

	ax::input::KeyEventHandler::cleanup_events(m_window);
}

void ax::Window::run_wgpu_render_pass(double delta, std::future<wgpu::SurfaceTexture> surfaceFuture)
{
	{
		PROFILER_SEGMENT_SCOPED_SUB_LEVEL(frame, render, pre_render);

		m_preRenderEventHandler.fire({ .delta = delta });
	}

	{
		PROFILER_SEGMENT_SCOPED_SUB_LEVEL(frame, render, surface_setup);
		PROFILER_NEW_SUBSEGMENT(surface_setup, create_texture);

		if (m_multisampleTexture.Get() == nullptr || m_multisampleTexture.GetWidth() != m_width || m_multisampleTexture.GetHeight() != m_height)
		{
			wgpu::TextureDescriptor msaaDesc
			{
				.usage = wgpu::TextureUsage::RenderAttachment,
				.dimension = wgpu::TextureDimension::e2D,
				.size = { m_width, m_height },
				.format = m_surfaceFormat,
				.mipLevelCount = 1,
				.sampleCount = 4,
				.viewFormatCount = 1,
				.viewFormats = &m_surfaceFormat,
			};
			m_multisampleTexture = m_device.CreateTexture(&msaaDesc);
		}

		wgpu::TextureViewDescriptor view
		{
			.nextInChain = nullptr,
			.label = "frame view",
			.format = m_multisampleTexture.GetFormat(),
			.dimension = wgpu::TextureViewDimension::e2D,
			.baseMipLevel = 0,
			.mipLevelCount = 1,
			.baseArrayLayer = 0,
			.arrayLayerCount = 1,
			.aspect = wgpu::TextureAspect::All,
		};
		wgpu::TextureView targetView{ m_multisampleTexture.CreateView(&view) };
		const auto surfaceTexture = surfaceFuture.get();
		const auto surfaceView = surfaceTexture.texture.CreateView();

		std::optional<FrameImageRequest> frameImageRequest{};
		{
			std::lock_guard lock{ m_frameImageRequestMutex };
			frameImageRequest = std::move(m_frameImageRequest);
			m_frameImageRequest.reset();
		}

		wgpu::Buffer frameImageReadback{};
		uint32_t frameImageBytesPerRow{};
		WindowFrameImageEvent frameImageError{};
		bool frameImageCopyQueued{ false };
		if (frameImageRequest)
		{
			const bool supportedFormat{
				m_surfaceFormat == wgpu::TextureFormat::BGRA8Unorm
					|| m_surfaceFormat == wgpu::TextureFormat::BGRA8UnormSrgb
					|| m_surfaceFormat == wgpu::TextureFormat::RGBA8Unorm
					|| m_surfaceFormat == wgpu::TextureFormat::RGBA8UnormSrgb
			};
			const auto rowBytes{ static_cast<uint64_t>(m_width) * 4 };
			const auto paddedRowBytes{ (rowBytes + 255) & ~uint64_t{ 255 } };
			if (!supportedFormat)
				frameImageError.message = "Unsupported surface format for frame image capture.";
			else if (surfaceTexture.texture.Get() == nullptr)
				frameImageError.message = "Cannot capture an invalid surface texture.";
			else if (paddedRowBytes > std::numeric_limits<uint32_t>::max())
				frameImageError.message = "Rendered frame row size exceeds WebGPU limits.";
			else
			{
				frameImageBytesPerRow = static_cast<uint32_t>(paddedRowBytes);
				wgpu::BufferDescriptor readbackDesc{};
				readbackDesc.size = paddedRowBytes * m_height;
				readbackDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
				readbackDesc.label = "Frame image readback";
				frameImageReadback = m_device.CreateBuffer(&readbackDesc);
				if (frameImageReadback.Get() == nullptr)
					frameImageError.message = "Failed to create frame image readback buffer.";
			}
		}

		wgpu::CommandEncoderDescriptor encoderDesc
		{
			.nextInChain = nullptr,
			.label = "frame encoder",
		};
		wgpu::CommandEncoder encoder = m_device.CreateCommandEncoder(&encoderDesc);

		float vp[8] = {
			static_cast<float>(m_width), static_cast<float>(m_height), 0.f, 0.f,
			m_camera.position().x, m_camera.position().y, m_camera.zoom(), 0.f,
		};
		m_queue.WriteBuffer(m_viewportBuffer, 0, vp, sizeof(vp));
		ShapeRenderer::init(m_device, {
			.colourFormat = m_surfaceFormat,
			.depthFormat = m_depthTextureFormat,
			.sampleCount = 4,
		});
		ShapeRenderer::begin_frame({
			.viewport = { static_cast<float>(m_width), static_cast<float>(m_height) },
			.cameraPosition = m_camera.position(),
			.zoom = m_camera.zoom(),
		});

		wgpu::RenderPassColorAttachment sceneColorAttachment
		{
			.view = targetView,
			.depthSlice = wgpu::kDepthSliceUndefined,
			.resolveTarget = nullptr,
			.loadOp = wgpu::LoadOp::Clear,
			.storeOp = wgpu::StoreOp::Store,
			.clearValue = m_clearColor,
		};
		wgpu::RenderPassDepthStencilAttachment sceneDepthAttachment
		{
			.view = m_depthTextureView,
			.depthLoadOp = wgpu::LoadOp::Clear,
			.depthStoreOp = wgpu::StoreOp::Store,
			.depthClearValue = 0.0f,
			.depthReadOnly = false,
			.stencilLoadOp = wgpu::LoadOp::Undefined,
			.stencilStoreOp = wgpu::StoreOp::Undefined,
			.stencilClearValue = 0,
			.stencilReadOnly = true,
		};
		wgpu::RenderPassDescriptor scenePassDesc{};
		scenePassDesc.colorAttachmentCount = 1;
		scenePassDesc.colorAttachments = &sceneColorAttachment;
		scenePassDesc.depthStencilAttachment = &sceneDepthAttachment;
		auto scenePass = encoder.BeginRenderPass(&scenePassDesc);
		{
			std::lock_guard lock{ m_pendingTextureMutex };
			m_pendingTextures = std::move(m_queuedTextures);
			m_queuedTextures.clear();
		}
		handle_render_pass(scenePass, delta);
		scenePass.End();

		wgpu::RenderPassColorAttachment oitColorAttachments[2]
		{
			{
				.view = m_oitAccumulationView,
				.depthSlice = wgpu::kDepthSliceUndefined,
				.resolveTarget = nullptr,
				.loadOp = wgpu::LoadOp::Clear,
				.storeOp = wgpu::StoreOp::Store,
				.clearValue = { 0.0, 0.0, 0.0, 0.0 },
			},
			{
				.view = m_oitRevealageView,
				.depthSlice = wgpu::kDepthSliceUndefined,
				.resolveTarget = nullptr,
				.loadOp = wgpu::LoadOp::Clear,
				.storeOp = wgpu::StoreOp::Store,
				.clearValue = { 1.0, 1.0, 1.0, 1.0 },
			},
		};
		wgpu::RenderPassDepthStencilAttachment oitDepthAttachment
		{
			.view = m_depthTextureView,
			.depthLoadOp = wgpu::LoadOp::Undefined,
			.depthStoreOp = wgpu::StoreOp::Undefined,
			.depthClearValue = 0.0f,
			.depthReadOnly = true,
			.stencilLoadOp = wgpu::LoadOp::Undefined,
			.stencilStoreOp = wgpu::StoreOp::Undefined,
			.stencilClearValue = 0,
			.stencilReadOnly = true,
		};
		wgpu::RenderPassDescriptor oitPassDesc{};
		oitPassDesc.colorAttachmentCount = 2;
		oitPassDesc.colorAttachments = oitColorAttachments;
		oitPassDesc.depthStencilAttachment = &oitDepthAttachment;
		auto oitPass = encoder.BeginRenderPass(&oitPassDesc);
		draw_transparent_sprites(oitPass);
		oitPass.End();

		wgpu::RenderPassColorAttachment compositeColorAttachment
		{
			.view = targetView,
			.depthSlice = wgpu::kDepthSliceUndefined,
			.resolveTarget = nullptr,
			.loadOp = wgpu::LoadOp::Load,
			.storeOp = wgpu::StoreOp::Store,
		};
		wgpu::RenderPassDescriptor compositePassDesc{};
		compositePassDesc.colorAttachmentCount = 1;
		compositePassDesc.colorAttachments = &compositeColorAttachment;
		auto compositePass = encoder.BeginRenderPass(&compositePassDesc);
		compositePass.SetPipeline(m_compositePipeline);
		compositePass.SetBindGroup(0, m_compositeBindGroup);
		compositePass.Draw(3, 1, 0, 0);
		compositePass.End();

		wgpu::RenderPassColorAttachment uiColorAttachment
		{
			.view = targetView,
			.depthSlice = wgpu::kDepthSliceUndefined,
			.resolveTarget = surfaceView,
			.loadOp = wgpu::LoadOp::Load,
			.storeOp = wgpu::StoreOp::Store,
		};
		wgpu::RenderPassDepthStencilAttachment uiDepthAttachment
		{
			.view = m_depthTextureView,
			.depthLoadOp = wgpu::LoadOp::Undefined,
			.depthStoreOp = wgpu::StoreOp::Undefined,
			.depthClearValue = 0.0f,
			.depthReadOnly = true,
			.stencilLoadOp = wgpu::LoadOp::Undefined,
			.stencilStoreOp = wgpu::StoreOp::Undefined,
			.stencilClearValue = 0,
			.stencilReadOnly = true,
		};
		wgpu::RenderPassDescriptor uiPassDesc{};
		uiPassDesc.colorAttachmentCount = 1;
		uiPassDesc.colorAttachments = &uiColorAttachment;
		uiPassDesc.depthStencilAttachment = &uiDepthAttachment;
		auto uiPass = encoder.BeginRenderPass(&uiPassDesc);
		{
			PROFILER_SEGMENT_SCOPED_SUB_LEVEL(frame, render, ui);
			render_gui(uiPass, delta);
		}
		uiPass.End();

		// Submit commands to GPU
		{
			PROFILER_SEGMENT_SCOPED_SUB_LEVEL(frame, render, finalise);

			wgpu::CommandBufferDescriptor bufferDesc
			{
				.nextInChain = nullptr,
				.label = "frame cmd buffer",
			};
			wgpu::CommandBuffer command{ encoder.Finish(&bufferDesc) };

			m_queue.Submit(1, &command);

			std::vector<PendingDrawBatch> pendingBatches{};
			{
				std::lock_guard lock{ m_pendingDrawMutex };
				pendingBatches = std::move(m_pendingDrawBatches);
				m_pendingDrawBatches.clear();
			}
			for (const auto& batch : pendingBatches)
				draw_pending_batch(batch, surfaceView);

			if (frameImageRequest && frameImageReadback.Get() != nullptr)
			{
				wgpu::TexelCopyTextureInfo source{};
				source.texture = surfaceTexture.texture;
				source.aspect = wgpu::TextureAspect::All;
				wgpu::TexelCopyBufferInfo destination{};
				destination.buffer = frameImageReadback;
				destination.layout.bytesPerRow = frameImageBytesPerRow;
				destination.layout.rowsPerImage = m_height;
				const wgpu::Extent3D extent{ m_width, m_height, 1 };
				wgpu::CommandEncoderDescriptor copyEncoderDesc{};
				copyEncoderDesc.label = "frame image copy encoder";
				auto copyEncoder{ m_device.CreateCommandEncoder(&copyEncoderDesc) };
				copyEncoder.CopyTextureToBuffer(&source, &destination, &extent);
				wgpu::CommandBufferDescriptor copyBufferDesc{};
				copyBufferDesc.label = "frame image copy command";
				auto copyCommand{ copyEncoder.Finish(&copyBufferDesc) };
				m_queue.Submit(1, &copyCommand);
				frameImageCopyQueued = true;
			}

			m_surface.Present();

			// Let the device progress
			m_device.Tick();
		}

		if (frameImageRequest)
		{
			if (frameImageCopyQueued)
				m_frameImageEventHandler.fire(read_frame_image(*frameImageRequest, frameImageReadback, frameImageBytesPerRow));
			else
			{
				frameImageError.compared = frameImageRequest->reference != nullptr;
				m_frameImageEventHandler.fire(std::move(frameImageError));
			}
		}
	}
}


void ax::Window::handle_render_pass(wgpu::RenderPassEncoder& pass, double delta)
{
	{
		PROFILER_SEGMENT_SCOPED_SUB_LEVEL(frame, render, render_event);

		// Send out the draw event
		m_renderEventHandler.fire
		({
			.delta = delta,
			.pass = pass
		});
	}

	{
		PROFILER_SEGMENT_SCOPED_SUB_LEVEL(frame, render, sprite_batch);

		// Group sprite records by texture so we can issue one instanced draw per texture.
		m_spriteGroupRanges.clear();
		for (auto* sprite : m_activeSprites)
		{
			if (sprite->tex == sprite->groupedTexture)
				continue;

			if (sprite->groupedTexture != nullptr)
			{
				auto& oldGroup = m_spriteGroups[sprite->groupedTexture];
				oldGroup.erase(std::remove(oldGroup.begin(), oldGroup.end(), sprite), oldGroup.end());
			}

			sprite->groupedTexture = sprite->tex;
			if (sprite->tex != nullptr)
				m_spriteGroups[sprite->tex].push_back(sprite);
		}

		for (auto& group : m_pendingSpriteGroups)
			group.second.clear();
		for (const auto& sprite : m_pendingTextures)
			if (sprite.tex != nullptr)
				m_pendingSpriteGroups[sprite.tex].push_back(&sprite);

		size_t spriteCount = 0;
		for (const auto& group : m_spriteGroups)
			spriteCount += group.second.size();
		for (const auto& group : m_pendingSpriteGroups)
			spriteCount += group.second.size();

		ensure_uniform_capacity(static_cast<uint32_t>(spriteCount));
		m_spriteGroupRanges.reserve(m_spriteGroups.size());
		m_spriteUploadData.reserve(spriteCount);

		uint32_t instanceCursor = 0;
		auto uploadGroup = [this, &instanceCursor](Texture* tex, const std::vector<const SpriteDefinition*>& activeSprites, const std::vector<const SpriteDefinition*>* pendingSprites)
		{
			m_spriteUploadData.clear();
			if (pendingSprites != nullptr)
				for (const auto* sprite : *pendingSprites)
					m_spriteUploadData.push_back(sprite->gpuData);
			for (const auto* sprite : activeSprites)
				if (!sprite->group || sprite->group->visible())
					m_spriteUploadData.push_back(sprite->gpuData);

			const uint32_t count = static_cast<uint32_t>(m_spriteUploadData.size());
			if (count == 0)
				return;

			m_spriteGroupRanges.push_back({ tex, instanceCursor, count });
			m_queue.WriteBuffer
			(
				m_uniforms,
				static_cast<uint64_t>(instanceCursor) * m_uniformStride,
				m_spriteUploadData.data(),
				m_spriteUploadData.size() * m_uniformStride
			);
			instanceCursor += count;
		};

		for (const auto& group : m_spriteGroups)
		{
			auto pendingGroup = m_pendingSpriteGroups.find(group.first);
			uploadGroup(group.first, group.second, pendingGroup == m_pendingSpriteGroups.end() ? nullptr : &pendingGroup->second);
		}
		static const std::vector<const SpriteDefinition*> emptyGroup;
		for (const auto& group : m_pendingSpriteGroups)
		{
			if (m_spriteGroups.find(group.first) == m_spriteGroups.end())
				uploadGroup(group.first, emptyGroup, &group.second);
		}

		draw_sprite_batches(pass, m_pipeline);
	}

	{
		PROFILER_SEGMENT_SCOPED_SUB_LEVEL(frame, render, debug_shapes);

		// After the sprites so translucent debug shapes (which don't write depth) blend over them.
		DebugShapes::flush(pass);
	}
}


void ax::Window::draw_sprite_batches(wgpu::RenderPassEncoder& pass, const wgpu::RenderPipeline& pipeline)
{
	pass.SetPipeline(pipeline);
	pass.SetBindGroup(1, m_viewportBindGroup);
	// Static batches precede ordinary sprites in both passes, with stable caller-specified order.
	for (const auto& batch : m_staticSpriteBatches)
	{
		if (!batch->ready() || !batch->visible())
			continue;
		pass.SetBindGroup(0, batch->m_bindGroup);
		pass.Draw(6, static_cast<std::uint32_t>(batch->size()));
	}
	for (const auto& range : m_spriteGroupRanges)
	{
		pass.SetBindGroup(0, get_texture_bind_group(range.tex));
		pass.Draw(6, range.count, 0, range.start);
	}
}

void ax::Window::draw_transparent_sprites(wgpu::RenderPassEncoder& pass)
{
	draw_sprite_batches(pass, m_oitPipeline);
	m_pendingTextures.clear();
}

void ax::Window::render_gui(wgpu::RenderPassEncoder& pass, double delta)
{
	ImGui_ImplWGPU_NewFrame();
	ImGui_ImplGlfw_NewFrame();

	ImGui::NewFrame();
	{
		m_uiEventHandler.fire({ .delta = delta });

		// Render notifications
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f); 
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.10f, 1.00f));
		{
			ImGui::RenderNotifications();
		}
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(1);
	}
	ImGui::EndFrame();

	ImGui::Render();

	ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass.Get());
}
