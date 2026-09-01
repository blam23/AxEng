#include "window.h"
#include "imgui_style.h"
#include "log_timer.h"

#include "spdlog/spdlog.h"
#include "backends/imgui_impl_wgpu.h"
#include "backends/imgui_impl_glfw.h"
#include <imgui.h>

#include <iostream>
#include <fstream>

static ImVec4 s_triColor{ 0.30f, 0.30f, 0.30f, 1.00f };

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
}

ax::Window::~Window()
{
	if (m_window)
		glfwDestroyWindow(m_window);

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
	ImGui_ImplGlfw_Shutdown();
	ImGui_ImplWGPU_Shutdown();
	glfwTerminate();
}

bool ax::Window::init_wegbpu()
{
	LogTimer _timer{ "wgpu setup" };

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
			if (status != wgpu::RequestAdapterStatus::Success) {
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
			if (status != wgpu::RequestDeviceStatus::Success) {
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

	wgpu::SurfaceCapabilities capabilities;
	m_surface.GetCapabilities(adapter, &capabilities);
	m_surfaceFormat = capabilities.formats[0];

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

	init_pipeline();
	return true;
}

void ax::Window::init_pipeline()
{
	LogTimer _timer{ "pipeline setup" };

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

	// Uniforms
	wgpu::BufferDescriptor bufferDesc{};
	bufferDesc.size = 4 * sizeof(float);
	bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform;
	bufferDesc.mappedAtCreation = false;
	bufferDesc.label = "Uniforms";
	m_uniforms = m_device.CreateBuffer(&bufferDesc);

	wgpu::BindGroupEntry uniformBinding{};
	uniformBinding.binding = 0;
	uniformBinding.buffer = m_uniforms;
	uniformBinding.offset = 0;
	uniformBinding.size = bufferDesc.size;

	// Layout
	wgpu::BindGroupLayoutEntry uniformEntry{};
	uniformEntry.binding = 0;
	uniformEntry.visibility = wgpu::ShaderStage::Fragment;
	uniformEntry.buffer.type = wgpu::BufferBindingType::Uniform;
	uniformEntry.buffer.minBindingSize = bufferDesc.size;

	wgpu::BindGroupLayoutDescriptor uniformLayoutDesc{};
	uniformLayoutDesc.entryCount = 1;
	uniformLayoutDesc.entries = &uniformEntry;
	wgpu::BindGroupLayout uniformLayout{ m_device.CreateBindGroupLayout(&uniformLayoutDesc) };

	wgpu::BindGroupDescriptor uniformGroupDesc{};
	uniformGroupDesc.layout = uniformLayout;
	uniformGroupDesc.entryCount = 1;
	uniformGroupDesc.entries = &uniformBinding;
	m_uniformGroup = m_device.CreateBindGroup(&uniformGroupDesc);

	wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{};
	pipelineLayoutDesc.bindGroupLayoutCount = 1;
	pipelineLayoutDesc.bindGroupLayouts = &uniformLayout;
	wgpu::PipelineLayout layout{ m_device.CreatePipelineLayout(&pipelineLayoutDesc) };

	pipelineDesc.layout = layout;

	m_pipeline = m_device.CreateRenderPipeline(&pipelineDesc);
}

void ax::Window::handle_tick(double delta)
{
	// Update Uniforms
	m_queue.WriteBuffer(m_uniforms, 0, &s_triColor, sizeof(float) * 3);

	m_updateEventHandler.fire({ .delta = delta });
}

bool ax::Window::init_imgui()
{
	LogTimer _timer{ "imgui setup" };

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::GetIO();

	ImGui_ImplGlfw_InitForOther(m_window, true);

	ImGui_ImplWGPU_InitInfo info{};
	info.Device = m_device.Get();
	info.DepthStencilFormat = static_cast<WGPUTextureFormat>(m_depthTextureFormat);
	info.RenderTargetFormat = static_cast<WGPUTextureFormat>(m_surfaceFormat);
	ImGui_ImplWGPU_Init(&info);

	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	ImGui::GetIO().Fonts->AddFontDefaultVector();
	ax::setup_imgui_style();

	return true;
}

double updatePrev{ 0 };
double updateDelta{ 0 };
void ax::Window::run_loop()
{
	while (!glfwWindowShouldClose(m_window))
	{
		glfwPollEvents();

		updateDelta = glfwGetTime() - updatePrev;
		updatePrev = glfwGetTime();

		handle_tick(updateDelta);
		run_wgpu_render_pass(updateDelta);
	}
}

void ax::Window::run_wgpu_render_pass(double delta)
{
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

	m_device.Tick();
}


void ax::Window::handle_render_pass(wgpu::RenderPassEncoder& pass, double delta)
{
	// Set Bindings
	pass.SetBindGroup(0, m_uniformGroup, 0, nullptr);

	// Draw Triangles
	pass.SetPipeline(m_pipeline);

	// Send out the draw event
	m_renderEventHandler.fire
	({
		.delta = delta,
		.pass = pass
	});

	// Draw imgui
	render_gui(pass, delta);
}

void ax::Window::render_gui(wgpu::RenderPassEncoder& pass, double delta)
{
	static ImVec4 clearColor{ 0.45f, 0.55f, 0.60f, 1.00f };

	ImGui_ImplWGPU_NewFrame();
	ImGui_ImplGlfw_NewFrame();

	ImGui::NewFrame();
	{
		//ImGui::DockSpaceOverViewport();

		ImGui::PushFont(nullptr, 16.0f);

		ImGui::BeginMainMenuBar();
		{
			ImGui::Text("AxEng");

			if (ImGui::Button("Reload Pipeline"))
			{
				init_pipeline();
			}

			ImGuiIO& io = ImGui::GetIO();
			ImGui::SameLine(ImGui::GetWindowWidth() - 425);
			ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
		}
		ImGui::EndMainMenuBar();

		ImGui::Begin("Test Window");
		{
			//ImGui::ColorEdit3("Clear Color", (float*)&clearColor);
			//m_clearColor.r = clearColor.x;
			//m_clearColor.g = clearColor.y;
			//m_clearColor.b = clearColor.z;

			ImGui::ColorEdit3("Triangle Color", (float*)&s_triColor);

		}
		ImGui::End();

		m_uiEventHandler.fire({ .delta = delta });

		ImGui::PopFont();
	}
	ImGui::EndFrame();
	ImGui::Render();

	ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass.Get());
}

