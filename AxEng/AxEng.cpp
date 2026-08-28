#include <iostream>
#include <chrono>

// Logging
#include "spdlog/spdlog.h"
#include "spdlog/stopwatch.h"

// Lua
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

// GFX
#include "glfw3webgpu.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <webgpu/webgpu_cpp.h>
#include <webgpu/webgpu_cpp_print.h>

int main()
{

    sol::state mainState;
	spdlog::stopwatch lua_timer;

	mainState.open_libraries
	(
		sol::lib::base,
		sol::lib::package,
		sol::lib::math,
		sol::lib::string,
		sol::lib::table,
		sol::lib::bit32
	);

	const auto res{ mainState.do_string("x = 4 * 9") };
	if (res.valid())
	{
		spdlog::info("Initialised lua, time taken: {}ms", std::chrono::duration_cast<std::chrono::nanoseconds>(lua_timer.elapsed()).count() / 1000000.);
	}
	else
	{
		sol::error err = res;
		spdlog::error("Failed to load init script: {}", err.what());
		return 1;
	}

	spdlog::stopwatch wgpu_timer;

	// Init WebGPU
	wgpu::InstanceDescriptor desc{};
	desc.nextInChain = nullptr;
	wgpu::Instance instance;
	wgpu::RequestAdapterOptions options {
		.featureLevel = wgpu::FeatureLevel::Core
	};
	wgpu::Adapter adapter;
	wgpu::Device device;
	wgpu::DeviceDescriptor deviceDescriptor{};

	static const auto kTimedWaitAny = wgpu::InstanceFeatureName::TimedWaitAny;
	wgpu::InstanceDescriptor instanceDesc {
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
		return 2;
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

	instance.WaitAny(adapter.RequestDevice(&deviceDescriptor, callbackMode, device_callback, (void*)&device), UINT64_MAX);
	if (device == nullptr) {
		spdlog::error("RequestDevice failed");
		return 3;
	}

	wgpu::Queue queue{ device.GetQueue() };

	// Init GLFW
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	GLFWwindow* window{ glfwCreateWindow(1240, 720, "AxEng", NULL, NULL) };

	// Here we create our WebGPU surface from the window!
	wgpu::Surface surface{ glfwGetWGPUSurface(instance.Get(), window) };

	wgpu::SurfaceCapabilities capabilities;
	surface.GetCapabilities(adapter, &capabilities);
	wgpu::TextureFormat surfaceFormat = capabilities.formats[0];

	wgpu::SurfaceConfiguration config {
		.nextInChain = nullptr,
		.device = device,
		.format = surfaceFormat,
		.usage = wgpu::TextureUsage::RenderAttachment,
		.width = 1240,
		.height = 720,
		.viewFormatCount = 0,
		.viewFormats = nullptr,
		.alphaMode = wgpu::CompositeAlphaMode::Auto,
		.presentMode = wgpu::PresentMode::Fifo,
	};
	surface.Configure(&config);

	spdlog::info("Initialised glfw window, time taken: {}ms", std::chrono::duration_cast<std::chrono::nanoseconds>(wgpu_timer.elapsed()).count() / 1000000.);

	// Terminate GLFW
	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();
	}

	surface.Unconfigure();
	glfwDestroyWindow(window);
	glfwTerminate();
}
