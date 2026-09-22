#include "window.h"
#include "imgui_style.h"
#include "imgui_helper.h"
#include "log_timer.h"

#include "spdlog/spdlog.h"

#include "backends/imgui_impl_wgpu.h"
#include "backends/imgui_impl_glfw.h"
#include <imgui.h>
#include "ImGuiNotify.hpp"
#include "IconsFontAwesome6.h"

#include <iostream>
#include <fstream>

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
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	
	return true;
}

void ax::teardown_glfw()
{
	glfwTerminate();
}

bool ax::Window::init_webgpu()
{
	//
	// Get wgpu Instance
	//
	wgpu::InstanceDescriptor desc{};
	desc.nextInChain = nullptr;
	wgpu::Instance instance;
	wgpu::RequestAdapterOptions options
	{
		.featureLevel = wgpu::FeatureLevel::Core
	};
	wgpu::Adapter adapter;
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

	auto callbackMode{ wgpu::CallbackMode::WaitAnyOnly };
	void* userdata{ &adapter };
	instance.WaitAny(instance.RequestAdapter(&options, callbackMode, adapter_callback, userdata), UINT64_MAX);
	if (adapter == nullptr)
	{
		spdlog::error("RequestAdapter failed");
		return false;
	}

	//
	// Get Device
	//
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

	instance.WaitAny(adapter.RequestDevice(&deviceDescriptor, callbackMode, device_callback, (void*)&m_device), UINT64_MAX);
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

	wgpu::SurfaceCapabilities capabilities{};
	if(m_surface.GetCapabilities(adapter, &capabilities))
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
		.sampleCount = 1,
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
	wgpu::SamplerDescriptor samplerDesc
	{
		.addressModeU = wgpu::AddressMode::ClampToEdge,
		.addressModeV = wgpu::AddressMode::ClampToEdge,
		.addressModeW = wgpu::AddressMode::ClampToEdge,
		.magFilter = wgpu::FilterMode::Nearest,
		.minFilter = wgpu::FilterMode::Nearest,
		.mipmapFilter = wgpu::MipmapFilterMode::Nearest,
	};
	m_nearestSampler = m_device.CreateSampler(&samplerDesc);

	reload_pipeline();
	return true;
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
		spdlog::warn("Could not load shader file, using fallback.");
		shaderCode = s_shader_source;
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
	depthStencilState.depthWriteEnabled = wgpu::OptionalBool::False;
	depthStencilState.depthCompare = wgpu::CompareFunction::Always;
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

	// Multisampling (off)
	pipelineDesc.multisample.count = 1;
	pipelineDesc.multisample.mask = ~0u;
	pipelineDesc.multisample.alphaToCoverageEnabled = false;

	// Uniforms (ring allocator). Each slot is 4 vec4 = 16 floats = 64 bytes
	// Dynamic uniform buffer offsets must be aligned to the device's required
	// minUniformBufferOffsetAlignment (commonly 256). Round the stride up.
	const size_t baseStride = 16 * sizeof(float);
	const size_t uniformAlignment = 256; // must be a power of two; queryable from device if needed
	m_uniformStride = ((baseStride + uniformAlignment - 1) / uniformAlignment) * uniformAlignment;
	wgpu::BufferDescriptor bufferDesc{};
	bufferDesc.size = static_cast<uint64_t>(m_uniformStride) * static_cast<uint64_t>(m_uniformsCapacity);
	bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform;
	bufferDesc.mappedAtCreation = false;
	bufferDesc.label = "UniformsRing";
	m_uniforms = m_device.CreateBuffer(&bufferDesc);

	//
	// Layout
	//

	auto bindGroupEntries{ std::vector<wgpu::BindGroupLayoutEntry>{ 3 } };

	// Uniforms
	bindGroupEntries[0].binding = 0;
	bindGroupEntries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
	bindGroupEntries[0].buffer.type = wgpu::BufferBindingType::Uniform;
	bindGroupEntries[0].buffer.minBindingSize = static_cast<uint64_t>(m_uniformStride);
	bindGroupEntries[0].buffer.hasDynamicOffset = true;
	

	// Texture
	bindGroupEntries[1].binding = 1;
	bindGroupEntries[1].visibility = wgpu::ShaderStage::Fragment;
	bindGroupEntries[1].texture.sampleType = wgpu::TextureSampleType::Float;
	bindGroupEntries[1].texture.viewDimension = wgpu::TextureViewDimension::e2D;
	
	// Sampler
	bindGroupEntries[2].binding = 2;
	bindGroupEntries[2].visibility = wgpu::ShaderStage::Fragment;
	bindGroupEntries[2].sampler.type = wgpu::SamplerBindingType::Filtering;

	// Bind em all up
	wgpu::BindGroupLayoutDescriptor layoutDesc{};
	layoutDesc.entryCount = bindGroupEntries.size();
	layoutDesc.entries = bindGroupEntries.data();
	m_groupLayout = m_device.CreateBindGroupLayout(&layoutDesc);

	wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{};
	pipelineLayoutDesc.bindGroupLayoutCount = 1;
	pipelineLayoutDesc.bindGroupLayouts = &m_groupLayout;
	wgpu::PipelineLayout layout{ m_device.CreatePipelineLayout(&pipelineLayoutDesc) };

	// Create the pipeline
	pipelineDesc.layout = layout;
	m_pipeline = m_device.CreateRenderPipeline(&pipelineDesc);

	// Default uniform values: pos_size (0,0,texW,texH), region (0,0,1,1), tint (1,1,1,1), viewport (width,height,0,0)
	float defaultUniforms[16] = { 0.f, 0.f, 0.f, 0.f,  // pos_size.x/y/width/height (width/height filled at draw time)
								  0.f, 0.f, 1.f, 1.f,  // region u0,v0,u1,v1
								  1.f, 1.f, 1.f, 1.f,  // tint
								  static_cast<float>(m_width), static_cast<float>(m_height), 0.f, 0.f };
	m_queue.WriteBuffer(m_uniforms, 0, defaultUniforms, sizeof(defaultUniforms));
}

void ax::Window::render_texture(Texture* tex, glm::vec2 position)
{
	m_pendingTextures.push_back
	({
		.tex = tex,
		.pos = position,
		.useRegion = false,
	});
}

void ax::Window::render_texture(Texture* tex, glm::vec2 position, rectf region)
{
	m_pendingTextures.push_back
	({
		.tex = tex,
		.pos = position,
		.region = region,
		.useRegion = true,
	});
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
	bindGroups[2].sampler = m_nearestSampler;

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
	glfwSetWindowCloseCallback(m_window, ax::Window::window_close_handler);
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
	ax::input::KeyEventHandler::register_events(m_window);
	ImGui_ImplGlfw_InstallCallbacks(m_window);

	while (!glfwWindowShouldClose(m_window))
	{
		glfwPollEvents();


		updateDelta = glfwGetTime() - updatePrev;
		updatePrev = glfwGetTime();

		handle_tick(updateDelta);
		run_wgpu_render_pass(updateDelta);
	}

	ax::input::KeyEventHandler::cleanup_events(m_window);
}

void ax::Window::run_wgpu_render_pass(double delta)
{
	m_preRenderEventHandler.fire({ .delta = delta });

	wgpu::SurfaceTexture surfaceTexture;
	m_surface.GetCurrentTexture(&surfaceTexture);

	if (surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal)
	{
		spdlog::error("Failed to create new frame surface");
		return;
	}

	wgpu::TextureViewDescriptor view
	{
		.nextInChain = nullptr,
		.label = "frame view",
		.format = surfaceTexture.texture.GetFormat(),
		.dimension = wgpu::TextureViewDimension::e2D,
		.baseMipLevel = 0,
		.mipLevelCount = 1,
		.baseArrayLayer = 0,
		.arrayLayerCount = 1,
		.aspect = wgpu::TextureAspect::All,
	};
	wgpu::TextureView targetView{ surfaceTexture.texture.CreateView(&view) };

	wgpu::CommandEncoderDescriptor encoderDesc
	{
		.nextInChain = nullptr,
		.label = "frame encoder",
	};
	wgpu::CommandEncoder encoder = m_device.CreateCommandEncoder(&encoderDesc);

	// Setup Render pass
	{
		// Clear frame
		wgpu::RenderPassColorAttachment colorAttachment
		{
			.view = targetView,
			.depthSlice = wgpu::kDepthSliceUndefined,
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
		wgpu::RenderPassEncoder pass{ encoder.BeginRenderPass(&passDesc) };

		handle_render_pass(pass, delta);

		pass.End();
	}

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


void ax::Window::handle_render_pass(wgpu::RenderPassEncoder& pass, double delta)
{
	// Send out the draw event
	m_renderEventHandler.fire
	({
		.delta = delta,
		.pass = pass
	});

	// Draw any queued textures
	for (const auto& p : m_pendingTextures)
	{
		if (p.tex == nullptr)
			continue;

		float uniforms[16]{};

		const auto width{ static_cast<float>(p.tex->width()) };
		const auto height{ static_cast<float>(p.tex->height()) };

		// Vertex position and size (screen space, pixels)
		uniforms[0] = p.pos.x;
		uniforms[1] = p.pos.y;
		uniforms[2] = (p.useRegion ? p.region.z : width) * p.scale.x;
		uniforms[3] = (p.useRegion ? p.region.w : height) * p.scale.y;

		// UVs (u0,v0,u1,v1) (texture space, 0-1)
		uniforms[4] = p.useRegion ? (p.region.x / width ) : 0.0f;
		uniforms[5] = p.useRegion ? (p.region.y / height) : 0.0f;
		uniforms[6] = p.useRegion ? (uniforms[4] + (p.region.z / width )) : 1.0f;
		uniforms[7] = p.useRegion ? (uniforms[5] + (p.region.w / height)) : 1.0f;

		// Tint
		uniforms[8] = 1.f; uniforms[9] = 1.f; uniforms[10] = 1.f; uniforms[11] = 1.f;

		// Viewport (pixels)
		uniforms[12] = static_cast<float>(m_width);
		uniforms[13] = static_cast<float>(m_height);

		// Unused
		uniforms[14] = 0.f; uniforms[15] = 0.f;

		// TODO: Batch calls, don't use this silly ring buffer either
		//   Idea 1: Have each sprite be a managed object that has it's own buffer and lifetime, no need for this ring buffer crap
		//   Idea 2: Batch all calls for same texture into a big buffer (would be much fewer draw calls, but much more effort)
		//   Might need to do both: have the sprite be a managed object that owns a slot in the big buffer
		//   Supporting one off draws should be simple too
		//   Also: Should this be attached to the window or the render pass? Why did I put this in the window class??
		const uint32_t stride = static_cast<uint32_t>(m_uniformStride);
		const uint32_t capacityBytes = stride * m_uniformsCapacity;
		uint32_t offset = m_uniformsOffset;

		if (offset + stride > capacityBytes)
			offset = 0;

		m_queue.WriteBuffer(m_uniforms, offset, uniforms, sizeof(uniforms));

		auto bindGroups{ std::vector<wgpu::BindGroupEntry>{ 3 } };

		bindGroups[0].binding = 0;
		bindGroups[0].buffer = m_uniforms;
		bindGroups[0].offset = 0; // dynamic offset will be provided at set time
		bindGroups[0].size = static_cast<uint64_t>(m_uniformStride);

		bindGroups[1].binding = 1;
		bindGroups[1].textureView = p.tex->view();

		bindGroups[2].binding = 2;
		bindGroups[2].sampler = m_nearestSampler;

		wgpu::BindGroupDescriptor bindGroupDesc{};
		bindGroupDesc.layout = m_groupLayout;
		bindGroupDesc.entryCount = bindGroups.size();
		bindGroupDesc.entries = bindGroups.data();
		wgpu::BindGroup binds = m_device.CreateBindGroup(&bindGroupDesc);

		uint32_t dynamicOffset = offset;
		pass.SetBindGroup(0, binds, 1, &dynamicOffset);

		m_uniformsOffset = offset + stride;
		pass.SetPipeline(m_pipeline);

		pass.Draw(6, 1, 0, 0);
	}

	// Draw imgui
	render_gui(pass, delta);

	// Clear the queue after rendering
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

