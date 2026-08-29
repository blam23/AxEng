#include "window.h"

#include "spdlog/spdlog.h"

void glfw_error_callback(int error, const char* description)
{
	spdlog::error("glfw error {}: {}", error, description);
}

ax::Window::Window(int width, int height, const std::string_view title)
{
	m_window = glfwCreateWindow(width, height, title.data(), NULL, NULL);
}

ax::Window::~Window()
{
	if (m_window)
		glfwDestroyWindow(m_window);

	m_surface.Unconfigure();
}

bool ax::Window::setup_glfw()
{
	glfwSetErrorCallback(glfw_error_callback);

	if (!glfwInit())
	{
		spdlog::error("glfw init failed");
		return false;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	
	return true;
}

void ax::Window::teardown_glfw()
{
	glfwTerminate();
}

bool ax::Window::init_wegbpu()
{
	// Init WebGPU
	wgpu::InstanceDescriptor desc{};
	desc.nextInChain = nullptr;
	wgpu::Instance instance;
	wgpu::RequestAdapterOptions options{
		.featureLevel = wgpu::FeatureLevel::Core
	};
	wgpu::Adapter adapter;
	wgpu::DeviceDescriptor deviceDescriptor{};

	static const auto kTimedWaitAny = wgpu::InstanceFeatureName::TimedWaitAny;
	wgpu::InstanceDescriptor instanceDesc{
		.requiredFeatureCount = 1,
		.requiredFeatures = &kTimedWaitAny
	};
	instance = wgpu::CreateInstance(&instanceDesc);

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
	if (adapter == nullptr) {
		spdlog::error("RequestAdapter failed");
		return false;
	}

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
	if (m_device == nullptr) {
		spdlog::error("RequestDevice failed");
		return false;
	}

	m_queue = m_device.GetQueue();

	// Here we create our WebGPU surface from the window!
	m_surface = wgpu::Surface{ glfwGetWGPUSurface(instance.Get(), m_window) };

	wgpu::SurfaceCapabilities capabilities;
	m_surface.GetCapabilities(adapter, &capabilities);
	wgpu::TextureFormat surfaceFormat = capabilities.formats[0];

	wgpu::SurfaceConfiguration config
	{
		.nextInChain = nullptr,
		.device = m_device,
		.format = surfaceFormat,
		.usage = wgpu::TextureUsage::RenderAttachment,
		.width = 1240,
		.height = 720,
		.viewFormatCount = 0,
		.viewFormats = nullptr,
		.alphaMode = wgpu::CompositeAlphaMode::Auto,
		.presentMode = wgpu::PresentMode::Fifo,
	};

	m_surface.Configure(&config);

	return true;
}

void ax::Window::run_loop()
{
	while (!glfwWindowShouldClose(m_window))
	{
		glfwPollEvents();
	}
}
