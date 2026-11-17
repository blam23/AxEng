#include "axeng/core/compute_program.h"
#include "axeng/core/gpu_readback_ring.h"

#include <gtest/gtest.h>
#include <dawn/native/DawnNative.h>
#include "axeng/core/windows_include.h"

#include <array>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace
{
	wgpu::Instance s_instance;
	wgpu::Device s_device;

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
		wgpu::DeviceDescriptor deviceDesc{};
		deviceDesc.SetUncapturedErrorCallback(
			[](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message)
			{
				ADD_FAILURE() << "WebGPU error: " << std::string_view{ message.data, message.length };
			});
		s_instance.WaitAny(adapter.RequestDevice(&deviceDesc, wgpu::CallbackMode::WaitAnyOnly,
			[](wgpu::RequestDeviceStatus status, wgpu::Device result, wgpu::StringView)
			{
				if (status == wgpu::RequestDeviceStatus::Success)
					s_device = std::move(result);
			}), UINT64_MAX);
	}

	wgpu::Buffer make_buffer(std::uint64_t size, wgpu::BufferUsage usage)
	{
		wgpu::BufferDescriptor desc{};
		desc.size = size;
		desc.usage = usage;
		return s_device.CreateBuffer(&desc);
	}

	// One pass sizes the next: "size" writes the workgroup count, "fill" (dispatched indirectly) writes tag + index.
	constexpr char s_shader[]{ R"(
@group(0) @binding(0) var<storage, read_write> args: array<u32, 3>;
@group(0) @binding(1) var<storage, read_write> values: array<u32, 8>;
@group(0) @binding(2) var<uniform> tag: vec4<u32>;
@compute @workgroup_size(1) fn size() { args[0] = 8u; args[1] = 1u; args[2] = 1u; }
@compute @workgroup_size(1) fn fill(@builtin(workgroup_id) id: vec3<u32>) { values[id.x] = tag.x * 100u + id.x; }
)" };
}

TEST(GpuReadbackRingTests, DeliversIndirectComputeResultsInOrderWithoutWaiting)
{
	if (!s_instance)
		initialise_device();
	if (!s_device)
		GTEST_SKIP() << "No D3D12 WebGPU device is available";

	const std::array<wgpu::BindGroupLayoutEntry, 3> layout{
		wgpu::BindGroupLayoutEntry{ .binding = 0, .visibility = wgpu::ShaderStage::Compute,
			.buffer = { .type = wgpu::BufferBindingType::Storage } },
		wgpu::BindGroupLayoutEntry{ .binding = 1, .visibility = wgpu::ShaderStage::Compute,
			.buffer = { .type = wgpu::BufferBindingType::Storage } },
		wgpu::BindGroupLayoutEntry{ .binding = 2, .visibility = wgpu::ShaderStage::Compute,
			.buffer = { .type = wgpu::BufferBindingType::Uniform } },
	};
	const std::array<std::string_view, 2> entries{ "size", "fill" };
	ax::ComputeProgram program;
	ASSERT_TRUE(program.initialise(s_device, { s_shader, sizeof(s_shader) - 1 }, layout, entries, "Readback test"));

	// A buffer bound read-write cannot also be the indirect source of a dispatch with that binding, so the arguments are
	// copied into a buffer of their own between the passes.
	const auto args{ make_buffer(12, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc) };
	const auto indirect{ make_buffer(12, wgpu::BufferUsage::Indirect | wgpu::BufferUsage::CopyDst) };
	const auto values{ make_buffer(32, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc) };
	const auto tag{ make_buffer(16, wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst) };
	const std::array<wgpu::BindGroupEntry, 3> bindings{
		wgpu::BindGroupEntry{ .binding = 0, .buffer = args, .size = 12 },
		wgpu::BindGroupEntry{ .binding = 1, .buffer = values, .size = 32 },
		wgpu::BindGroupEntry{ .binding = 2, .buffer = tag, .size = 16 },
	};
	const auto group{ program.create_bind_group(s_device, bindings) };
	ASSERT_TRUE(group);

	ax::GpuReadbackRing ring;
	ASSERT_TRUE(ring.initialise(s_device, 32, 2, "Test ring"));
	EXPECT_EQ(ring.slot_bytes(), 32u);

	const auto queue{ s_device.GetQueue() };
	for (std::uint32_t frame{ 1 }; frame <= 3; ++frame)
	{
		const std::array<std::uint32_t, 4> tagValue{ frame, 0, 0, 0 };
		queue.WriteBuffer(tag, 0, tagValue.data(), sizeof(tagValue));
		auto encoder{ s_device.CreateCommandEncoder() };
		ASSERT_TRUE(program.encode(encoder, "size", group, 1));
		encoder.CopyBufferToBuffer(args, 0, indirect, 0, 12);
		ASSERT_TRUE(program.encode_indirect(encoder, "fill", group, indirect));
		const bool copied{ ring.encode_copy(encoder, values, 0, frame) };
		// Two slots: the third copy is refused while the first two have not been handed over.
		EXPECT_EQ(copied, frame <= 2);
		const auto commands{ encoder.Finish() };
		queue.Submit(1, &commands);
		ring.submitted();
	}
	EXPECT_EQ(ring.pending(), 2u);

	std::vector<std::uint64_t> tags{};
	std::vector<std::uint32_t> firsts{}, lasts{};
	const auto deadline{ std::chrono::steady_clock::now() + std::chrono::seconds{ 10 } };
	while (tags.size() < 2 && std::chrono::steady_clock::now() < deadline)
	{
		s_device.Tick();
		ring.poll([&](std::uint64_t slotTag, std::span<const std::byte> data)
			{
				std::array<std::uint32_t, 8> read{};
				ASSERT_EQ(data.size(), sizeof(read));
				std::memcpy(read.data(), data.data(), sizeof(read));
				tags.push_back(slotTag);
				firsts.push_back(read[0]);
				lasts.push_back(read[7]);
			});
		std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
	}
	ASSERT_EQ(tags.size(), 2u);
	EXPECT_EQ(tags[0], 1u);
	EXPECT_EQ(tags[1], 2u);
	// Each copy was recorded after its frame's fill, so it carries that frame's values; frame 3 overwrote the buffer
	// after copy 2 was recorded, which the copy does not see.
	EXPECT_EQ(firsts[0], 100u);
	EXPECT_EQ(lasts[0], 107u);
	EXPECT_EQ(firsts[1], 200u);
	EXPECT_EQ(lasts[1], 207u);
	EXPECT_EQ(ring.pending(), 0u);

	auto encoder{ s_device.CreateCommandEncoder() };
	EXPECT_TRUE(ring.encode_copy(encoder, values, 0, 4));
	ring.clear();
	EXPECT_FALSE(ring.ready());
}
