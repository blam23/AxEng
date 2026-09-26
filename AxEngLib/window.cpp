#include "imgui_helper.h"
#include "imgui_style.h"
#include "log_timer.h"
#include "perf_profiler.h"
#include "window.h"
#include "mouse.h"

#include "spdlog/spdlog.h"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_wgpu.h"
#include "IconsFontAwesome6.h"
#include "ImGuiNotify.hpp"
#include <imgui.h>

#include <algorithm>
#include <fstream>
#include <iostream>

std::map<GLFWwindow*, ax::Window*> s_windows{};
std::mutex s_windows_mutex{};

static void glfw_error_callback(int error, const char* description)
{
	spdlog::error("glfw error {}: {}", error, description);
}

ax::Window::Window(const WindowDefinition& def)
	: m_width{ def.width },
	m_height{ def.height },
	m_vsync{ def.vsync }
{
	LogTimer _timer{ "create window" };
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
				ImGui_ImplGlfw_Shutdown();
				ImGui_ImplWGPU_Shutdown();
			}
		}
		glfwDestroyWindow(m_window);
	}

	m_surface.Unconfigure();
}

void ax::Window::resize_event_handler(GLFWwindow* window, int width, int height)
{
	std::lock_guard lock{ s_windows_mutex };
	auto it = s_windows.find(window);
	if (it != s_windows.end())
	{
		it->second->m_width = width;
		it->second->m_height = height;
		it->second->create_surfaces();
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
	//glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	
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
	if (m_surface.GetCapabilities(m_adapter, &capabilities))
		m_surfaceFormat = capabilities.formats[0];
	else
	{
		spdlog::error("Failed to get surface capabilities..");
		return false;
	}

	wgpu::SurfaceConfiguration config
	{
		.nextInChain = nullptr,
		.device = m_device,
		.format = m_surfaceFormat,
		.usage = wgpu::TextureUsage::RenderAttachment,
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

bool ax::Window::init_webgpu()
{
	//
	// Get wgpu Instance
	//
	wgpu::InstanceDescriptor desc{};
	wgpu::Instance instance;
	desc.nextInChain = nullptr;

	static const auto kTimedWaitAny = wgpu::InstanceFeatureName::TimedWaitAny;
	wgpu::InstanceDescriptor instanceDesc
	{
		.requiredFeatureCount = 1,
		.requiredFeatures = &kTimedWaitAny
	};
	instance = wgpu::CreateInstance(&instanceDesc);
	
	//
	// Get Adapter
	//
	wgpu::RequestAdapterOptions options
	{
		.featureLevel = wgpu::FeatureLevel::Core
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
	// Enable depth writes and normal depth testing so z-index in instance data
	// can determine ordering when batching across textures.
	depthStencilState.depthWriteEnabled = wgpu::OptionalBool::True;
	depthStencilState.depthCompare = wgpu::CompareFunction::LessEqual;
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
	fragmentState.entryPoint = "fs_main";
	fragmentState.constantCount = 0;
	fragmentState.constants = nullptr;
	wgpu::BlendState blendState;
	wgpu::ColorTargetState colorTarget;
	colorTarget.format = m_surfaceFormat;
	colorTarget.blend = &blendState;
	colorTarget.writeMask = wgpu::ColorWriteMask::All;
	fragmentState.targetCount = 1;
	fragmentState.targets = &colorTarget;
	pipelineDesc.fragment = &fragmentState;
	blendState.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
	blendState.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
	blendState.color.operation = wgpu::BlendOperation::Add;

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
	vpDesc.size = 4 * sizeof(float);
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
	globalEntries[0].buffer.minBindingSize = static_cast<uint64_t>(4 * sizeof(float));

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

	m_textureBindGroups.clear();
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
	return sprite;
}

void ax::Window::free_sprite(SpriteDefinition* sprite)
{
	const auto activeSprite = std::find(m_activeSprites.begin(), m_activeSprites.end(), sprite);
	if (activeSprite == m_activeSprites.end())
		return;

	if (sprite->groupedTexture != nullptr)
	{
		auto group = m_spriteGroups.find(sprite->groupedTexture);
		if (group != m_spriteGroups.end())
			group->second.erase(std::remove(group->second.begin(), group->second.end(), sprite), group->second.end());
	}

	*activeSprite = m_activeSprites.back();
	m_activeSprites.pop_back();
	*sprite = {};
	m_freeSpriteSlots.push_back(sprite);
}

std::size_t ax::Window::get_sprite_count() const
{
	return m_activeSprites.size();
}

void ax::Window::render_texture(Texture* tex, glm::vec2 position)
{
	SpriteDefinition sprite{};
	sprite.tex = tex;
	sprite.gpuData.pos = position;
	m_pendingTextures.push_back(sprite);
}

void ax::Window::render_texture(Texture* tex, glm::vec2 position, rectf region)
{
	SpriteDefinition sprite{};
	sprite.tex = tex;
	sprite.gpuData.pos = position;
	sprite.gpuData.region = region;
	sprite.gpuData.useRegion = 1;
	m_pendingTextures.push_back(sprite);
}

void ax::Window::render_texture(Texture* tex, glm::vec2 position, float r, rectf region, float z, glm::vec4 color, glm::vec2 scale)
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
	m_pendingTextures.push_back(sprite);
}

void ax::Window::render_texture(Texture* tex, glm::vec2 position, float z)
{
	SpriteDefinition sprite{};
	sprite.tex = tex;
	sprite.gpuData.pos = position;
	sprite.gpuData.z = z;
	m_pendingTextures.push_back(sprite);
}

void ax::Window::render_texture(Texture* tex, glm::vec2 position, rectf region, float z)
{
	SpriteDefinition sprite{};
	sprite.tex = tex;
	sprite.gpuData.pos = position;
	sprite.gpuData.region = region;
	sprite.gpuData.useRegion = 1;
	sprite.gpuData.z = z;
	m_pendingTextures.push_back(sprite);
}

void ax::Window::call_deferred(std::function<void()> func)
{
	m_deferred.push_back(std::move(func));
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
	m_textureBindGroups.clear();
}

wgpu::BindGroup ax::Window::setup_bind_groups(const wgpu::TextureView& view)
{
	auto bindGroups{ std::vector<wgpu::BindGroupEntry>{ 3 } };

	// Uniforms
	bindGroups[0].binding = 0;
	bindGroups[0].buffer = m_uniforms;
	bindGroups[0].offset = 0;
	bindGroups[0].size = m_uniforms.GetSize();

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

	// Default font
	ImGuiIO& io = ImGui::GetIO();
	ImFontConfig default_font_config{};
	const auto baseFontSize = 15.0f;
	default_font_config.SizePixels = baseFontSize;
	default_font_config.ExtraSizeScale = 1.0f;
	io.Fonts->AddFontFromFileTTF("../fonts/Montserrat-Medium.ttf", baseFontSize, &default_font_config);

	// Merge in icons from Font Awesome
	const auto iconFontSize = baseFontSize * 2.0f / 3.0f;
	static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
	ImFontConfig icons_config{};
	icons_config.MergeMode = true;
	icons_config.PixelSnapH = true;
	icons_config.GlyphMinAdvanceX = iconFontSize;
	icons_config.SizePixels = iconFontSize;
	icons_config.ExtraSizeScale = 1.0f;
	const auto merged_font{ io.Fonts->AddFontFromFileTTF(FONT_ICON_FILE_NAME_FAS, iconFontSize, &icons_config, icons_ranges) };
	ax::ImGuiHelper::add_font("default", merged_font);

	// Add a monospace font
	ImFontConfig mono_font_config{};
	mono_font_config.SizePixels = baseFontSize;
	mono_font_config.ExtraSizeScale = 1.0f;
	const auto mono_font{ io.Fonts->AddFontFromFileTTF("../fonts/SourceCodePro-Medium.ttf", baseFontSize, &mono_font_config) };
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

		updateDelta = glfwGetTime() - updatePrev;
		updatePrev = glfwGetTime();

		PROFILER_TOP_LEVEL(frame);
		PROFILER_TOP_LEVEL_SEGMENT_SCOPED(frame);
		{
			PROFILER_SEGMENT_SCOPED(frame, tick);
			handle_tick(updateDelta);
		}

		{
			PROFILER_SEGMENT_SCOPED(frame, render);
			run_wgpu_render_pass(updateDelta);
		}

		for (auto& func : m_deferred)
			func();
		m_deferred.clear();
	}

	ax::input::KeyEventHandler::cleanup_events(m_window);
}

void ax::Window::run_wgpu_render_pass(double delta)
{
	{
		PROFILER_SEGMENT_SCOPED_SUB_LEVEL(frame, render, pre_render);

		m_preRenderEventHandler.fire({ .delta = delta });
	}

	{
		wgpu::RenderPassEncoder pass;
		wgpu::CommandEncoder encoder;

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

			wgpu::CommandEncoderDescriptor encoderDesc
			{
				.nextInChain = nullptr,
				.label = "frame encoder",
			};
			encoder = m_device.CreateCommandEncoder(&encoderDesc);

			// Update global viewport uniform (width,height,0,0)
			{
				float vp[4] = { static_cast<float>(m_width), static_cast<float>(m_height), 0.f, 0.f };
				m_queue.WriteBuffer(m_viewportBuffer, 0, vp, sizeof(vp));
			}

			// Setup Render pass
			{
				PROFILER_SUBSEGMENT_CHANGE_FROM_TO(surface_setup, create_texture, get_surface_texture);

				// Get current render surface
				wgpu::SurfaceTexture surfaceTexture;
				m_surface.GetCurrentTexture(&surfaceTexture);
				if (surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal)
				{
					spdlog::error("Failed to get new frame surface");
					return;
				}

				PROFILER_SUBSEGMENT_CHANGE_FROM_TO(surface_setup, get_surface_texture, begin_pass);

				// Clear frame
				wgpu::RenderPassColorAttachment colorAttachment
				{
					.view = targetView,
					.depthSlice = wgpu::kDepthSliceUndefined,
					.resolveTarget = surfaceTexture.texture.CreateView(),
					.loadOp = wgpu::LoadOp::Clear,
					.storeOp = wgpu::StoreOp::Store,
					.clearValue = m_clearColor,
				};

				// Clear depth
				wgpu::RenderPassDepthStencilAttachment depthAttachment
				{
					.view = m_depthTextureView,
					.depthLoadOp = wgpu::LoadOp::Clear,
					.depthStoreOp = wgpu::StoreOp::Store,
					.depthClearValue = 1.0f,
					.depthReadOnly = false,
					.stencilLoadOp = wgpu::LoadOp::Undefined,
					.stencilStoreOp = wgpu::StoreOp::Undefined,
					.stencilClearValue = 0,
					.stencilReadOnly = true,
				};

				wgpu::RenderPassDescriptor passDesc
				{
					.nextInChain = nullptr,
					.colorAttachmentCount = 1,
					.colorAttachments = &colorAttachment,
					.depthStencilAttachment = &depthAttachment,
					.timestampWrites = nullptr,
				};
				pass = encoder.BeginRenderPass(&passDesc);

				PROFILER_END_SUBSEGMENT(begin_pass);
			}
		}

		// Build the actual render pass (lua, sprite batching, imgui, etc.)
		{
			handle_render_pass(pass, delta);
			pass.End();
		}

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

			m_surface.Present();

			// Let the device progress
			m_device.Tick();
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

		pass.SetPipeline(m_pipeline);
		// Set the global viewport bind group (group 1)
		pass.SetBindGroup(1, m_viewportBindGroup);

		for (const auto& r : m_spriteGroupRanges)
		{
			auto it = m_textureBindGroups.find(r.tex);
			if (it == m_textureBindGroups.end())
				it = m_textureBindGroups.emplace(r.tex, setup_bind_groups(r.tex->view())).first;
			pass.SetBindGroup(0, it->second);
			pass.Draw(6, r.count, 0, r.start);
		}

		m_pendingTextures.clear();
	}

	{
		PROFILER_SEGMENT_SCOPED_SUB_LEVEL(frame, render, ui);

		render_gui(pass, delta);
	}
}

void ax::Window::render_gui(wgpu::RenderPassEncoder& pass, double delta)
{
	static ImVec4 clearColor{ 0.45f, 0.55f, 0.60f, 1.00f };

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

