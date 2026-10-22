#include "axeng/core/particles/particle_system.h"

#include <gtest/gtest.h>
#include <dawn/native/DawnNative.h>
#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	wgpu::Instance s_instance;
	wgpu::Device s_device;
	std::atomic<bool> s_uncapturedError{ false };
	std::mutex s_errorMutex;
	std::vector<std::string> s_errorMessages;

	void initialise_device()
	{
		char systemDirectory[MAX_PATH]{};
		const auto length{ ::GetSystemDirectoryA(systemDirectory, MAX_PATH) };
		if (length == 0 || length >= MAX_PATH)
			return;
		const std::string systemPath{ std::string{ systemDirectory, length } + '\\' };
		const char* searchPaths[]{ systemPath.c_str() };
		dawn::native::DawnInstanceDescriptor dawnDesc{};
		dawnDesc.additionalRuntimeSearchPathsCount = 1;
		dawnDesc.additionalRuntimeSearchPaths = searchPaths;
		static const auto s_timedWaitAny{ wgpu::InstanceFeatureName::TimedWaitAny };
		wgpu::InstanceDescriptor instanceDesc{};
		instanceDesc.nextInChain = &dawnDesc;
		instanceDesc.requiredFeatureCount = 1;
		instanceDesc.requiredFeatures = &s_timedWaitAny;
		s_instance = wgpu::CreateInstance(&instanceDesc);
		if (!s_instance)
			return;

		wgpu::RequestAdapterOptions options{};
		options.backendType = wgpu::BackendType::D3D12;
		wgpu::Adapter adapter;
		s_instance.WaitAny(s_instance.RequestAdapter(&options, wgpu::CallbackMode::WaitAnyOnly,
			[&adapter](wgpu::RequestAdapterStatus status, wgpu::Adapter result, wgpu::StringView)
			{
				if (status == wgpu::RequestAdapterStatus::Success)
					adapter = std::move(result);
			}), UINT64_MAX);
		if (!adapter)
			return;

		wgpu::Limits limits{};
		limits.maxBindGroups = 2;
		limits.maxVertexBuffers = 1;
		limits.maxBufferSize = 150000 * sizeof(wgpu::VertexAttribute);
		limits.maxVertexAttributes = 5;
		wgpu::DeviceDescriptor deviceDesc{};
		deviceDesc.requiredLimits = &limits;
		deviceDesc.SetUncapturedErrorCallback(
			[](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message)
			{
				if (message.data != nullptr && message.length > 0)
				{
					std::lock_guard lock{ s_errorMutex };
					s_errorMessages.emplace_back(message.data, message.length);
					s_uncapturedError.store(true, std::memory_order_relaxed);
				}
			});
		s_instance.WaitAny(adapter.RequestDevice(&deviceDesc, wgpu::CallbackMode::WaitAnyOnly,
			[](wgpu::RequestDeviceStatus status, wgpu::Device result, wgpu::StringView)
			{
				if (status == wgpu::RequestDeviceStatus::Success)
					s_device = std::move(result);
			}), UINT64_MAX);
	}

	wgpu::Texture create_attachment(const wgpu::Device& device, wgpu::TextureFormat format)
	{
		wgpu::TextureDescriptor desc{};
		desc.size = { 32, 32, 1 };
		desc.format = format;
		desc.sampleCount = 1;
		desc.usage = wgpu::TextureUsage::RenderAttachment;
		return device.CreateTexture(&desc);
	}
}

TEST(ParticleSystemTests, ValidatesEmitterParameters)
{
	ax::ParticleConfig config{};
	EXPECT_FALSE(ax::validate_particle_config(config));
	config.explosiveness = 1.1f;
	EXPECT_TRUE(ax::validate_particle_config(config));
	config.explosiveness = 0.5f;
	config.duration = 0.009f;
	EXPECT_TRUE(ax::validate_particle_config(config));
	config.duration = 0.01f;
	EXPECT_FALSE(ax::validate_particle_config(config));
	config.capacity = 0;
	EXPECT_TRUE(ax::validate_particle_config(config));
}

TEST(ParticleSystemTests, ComputesAndDrawsWithGpuResidentState)
{
	if (!s_instance)
		initialise_device();
	if (!s_device)
		GTEST_SKIP() << "No D3D12 WebGPU device is available";

	s_uncapturedError.store(false, std::memory_order_relaxed);
	{
		std::lock_guard lock{ s_errorMutex };
		s_errorMessages.clear();
	}
	ax::ParticleSystem particles;
	ax::ParticleConfig config{};
	config.capacity = 64;
	config.emissionRate = 64;
	config.duration = 1.0f;
	config.explosiveness = 1.0f;
	config.lifetimeMin = 1.0f;
	config.lifetimeMax = 1.0f;
	config.speedMin = 0.0f;
	config.speedMax = 0.0f;
	config.sizeStart = 8.0f;
	config.sizeEnd = 8.0f;
	auto emitter{ particles.create(config) };
	emitter->play();

	auto encoder{ s_device.CreateCommandEncoder() };
	auto queue{ s_device.GetQueue() };
	particles.encode_compute(s_device, queue, encoder, 1.0 / 60.0,
		{ -16.0f, -16.0f, 16.0f, 16.0f }, { 32.0f, 32.0f }, 1.0f,
		wgpu::TextureFormat::RGBA8Unorm, 1);

	auto accumulation{ create_attachment(s_device, wgpu::TextureFormat::RGBA16Float) };
	auto revealage{ create_attachment(s_device, wgpu::TextureFormat::R8Unorm) };
	auto depth{ create_attachment(s_device, wgpu::TextureFormat::Depth24Plus) };
	wgpu::TextureView accumulationView{ accumulation.CreateView() };
	wgpu::TextureView revealageView{ revealage.CreateView() };
	wgpu::TextureView depthView{ depth.CreateView() };
	wgpu::RenderPassColorAttachment colors[2]{};
	colors[0].view = accumulationView;
	colors[0].loadOp = wgpu::LoadOp::Clear;
	colors[0].storeOp = wgpu::StoreOp::Store;
	colors[1].view = revealageView;
	colors[1].loadOp = wgpu::LoadOp::Clear;
	colors[1].storeOp = wgpu::StoreOp::Store;
	wgpu::RenderPassDepthStencilAttachment depthAttachment{};
	depthAttachment.view = depthView;
	depthAttachment.depthReadOnly = true;
	depthAttachment.depthLoadOp = wgpu::LoadOp::Undefined;
	depthAttachment.depthStoreOp = wgpu::StoreOp::Undefined;
	wgpu::RenderPassDescriptor passDesc{};
	passDesc.colorAttachmentCount = 2;
	passDesc.colorAttachments = colors;
	passDesc.depthStencilAttachment = &depthAttachment;
	auto pass{ encoder.BeginRenderPass(&passDesc) };
	particles.draw(pass);
	pass.End();

	auto command{ encoder.Finish() };
	queue.Submit(1, &command);
	s_device.Tick();
	if (s_uncapturedError.load(std::memory_order_relaxed))
	{
		std::lock_guard lock{ s_errorMutex };
		for (const auto& message : s_errorMessages)
			ADD_FAILURE() << message;
	}
	particles.shutdown();
}
