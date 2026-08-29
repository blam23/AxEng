#include "window.h"
#include "imgui_style.h"
#include "log_timer.h"

#include "spdlog/spdlog.h"
#include "backends/imgui_impl_wgpu.h"
#include "backends/imgui_impl_glfw.h"
#include <imgui.h>

void glfw_error_callback(int error, const char* description)
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
		.presentMode = m_vsync ? wgpu::PresentMode::Mailbox : wgpu::PresentMode::Immediate,
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

	return true;
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

void ax::Window::run_loop()
{
	while (!glfwWindowShouldClose(m_window))
	{
		glfwPollEvents();

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

			static ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

			ImGui_ImplWGPU_NewFrame();
			ImGui_ImplGlfw_NewFrame();

			ImGui::NewFrame();
			{
				//ImGui::DockSpaceOverViewport();

				ImGui::PushFont(nullptr, 16.0f);

				ImGui::BeginMainMenuBar();
				{
					ImGui::Text("AxEng");

					ImGuiIO& io = ImGui::GetIO();
					ImGui::SameLine(ImGui::GetWindowWidth() - 425);
					ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
				}
				ImGui::EndMainMenuBar();

				ImGui::Begin("Test Window");
				{
					ImGui::ColorEdit3("Clear Color", (float*)&clear_color);

					m_clearColor.r = clear_color.x;
					m_clearColor.g = clear_color.y;
					m_clearColor.b = clear_color.z;
				}
				ImGui::End();

				ImGui::PopFont();
			}
			ImGui::EndFrame();
			ImGui::Render();

			ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass.Get());

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
}
