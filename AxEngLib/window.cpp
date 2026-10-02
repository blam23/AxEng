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
	: m_width{ def.width }
	, m_height{ def.height }
	, m_vsync{ def.vsync }
{
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
		if (it->second == nullptr)
		{
			spdlog::error("Window list contains null window?");
			return;
		}

		it->second->m_width = width == 0 ? 1 : width;
		it->second->m_height = height == 0 ? 1 : height;
		it->second->create_surfaces();
		it->second->m_resizeEventHandler.fire({ static_cast<uint32_t>(width), static_cast<uint32_t>(height) });
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

	depthStencilState.depthWriteEnabled = wgpu::OptionalBool::False;
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

	m_textureBindGroups.clear();
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

	ImGui_ImplGlfw_RestoreCallbacks(m_window);

	const auto hwnd{ ::glfwGetWin32Window(m_window) };
	s_redirectHandles.erase(hwnd);
	::SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)s_originalWndProc[hwnd]);
	s_originalWndProc.erase(hwnd);

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
			std::lock_guard lock{ m_deferredMutex };
			for (auto& func : m_deferred)
				func();
			m_deferred.clear();
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

		wgpu::CommandEncoderDescriptor encoderDesc
		{
			.nextInChain = nullptr,
			.label = "frame encoder",
		};
		wgpu::CommandEncoder encoder = m_device.CreateCommandEncoder(&encoderDesc);

		float vp[4] = { static_cast<float>(m_width), static_cast<float>(m_height), 0.f, 0.f };
		m_queue.WriteBuffer(m_viewportBuffer, 0, vp, sizeof(vp));

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

		draw_sprite_batches(pass, m_pipeline);
	}
}


void ax::Window::draw_sprite_batches(wgpu::RenderPassEncoder& pass, const wgpu::RenderPipeline& pipeline)
{
	pass.SetPipeline(pipeline);
	pass.SetBindGroup(1, m_viewportBindGroup);
	for (const auto& range : m_spriteGroupRanges)
	{
		auto it = m_textureBindGroups.find(range.tex);
		if (it == m_textureBindGroups.end())
			it = m_textureBindGroups.emplace(range.tex, setup_bind_groups(range.tex->view())).first;
		pass.SetBindGroup(0, it->second);
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

