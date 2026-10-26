#include <gtest/gtest.h>
#include <dawn/native/DawnNative.h>
#include <windows.h>

#include "axeng/core/lighting/light_renderer.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <numbers>
#include <string>
#include <vector>

namespace
{
	constexpr std::uint32_t s_size{ 128 };

	using Pixel = std::array<int, 4>;

	struct Image
	{
		std::vector<std::uint8_t> rgba;

		Pixel at(std::uint32_t x, std::uint32_t y) const
		{
			const auto* p{ &rgba[(y * s_size + x) * 4] };
			return { p[0], p[1], p[2], p[3] };
		}
	};

	testing::AssertionResult close_to(const Pixel& actual, const Pixel& expected, int tolerance)
	{
		for (std::size_t i = 0; i < 4; ++i)
			if (std::abs(actual[i] - expected[i]) > tolerance)
				return testing::AssertionFailure() << "pixel is (" << actual[0] << ", " << actual[1] << ", "
					<< actual[2] << ", " << actual[3] << ") expected (" << expected[0] << ", " << expected[1] << ", "
					<< expected[2] << ", " << expected[3] << ")";
		return testing::AssertionSuccess();
	}

	// The world origin is at the centre of the target.
	ax::LightView centred_view()
	{
		return { .viewport = { static_cast<float>(s_size), static_cast<float>(s_size) } };
	}

	ax::LightingSettings settings(glm::vec3 ambient, float maxExposure = 1.0f)
	{
		return { .enabled = true, .ambient = ambient, .resolutionDivisor = 2, .maxExposure = maxExposure };
	}
}

class LightRendererTest : public testing::Test
{
protected:
	static void SetUpTestSuite()
	{
		char systemDirectory[MAX_PATH]{};
		const auto length{ ::GetSystemDirectoryA(systemDirectory, MAX_PATH) };
		const std::string systemPath{ std::string(systemDirectory, length) + '\\' };
		const char* searchPaths[]{ systemPath.c_str() };
		dawn::native::DawnInstanceDescriptor dawnDesc{};
		dawnDesc.additionalRuntimeSearchPathsCount = 1;
		dawnDesc.additionalRuntimeSearchPaths = searchPaths;

		static const auto s_timed_wait_any{ wgpu::InstanceFeatureName::TimedWaitAny };
		wgpu::InstanceDescriptor instanceDesc{};
		instanceDesc.nextInChain = &dawnDesc;
		instanceDesc.requiredFeatureCount = 1;
		instanceDesc.requiredFeatures = &s_timed_wait_any;
		s_instance = wgpu::CreateInstance(&instanceDesc);
		if (s_instance == nullptr)
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
		if (adapter == nullptr)
			return;

		// Same limits the engine asks for, so the renderer is tested against them.
		wgpu::Limits limits{};
		limits.maxBindGroups = 2;
		limits.maxVertexBuffers = 1;
		limits.maxVertexAttributes = 5;

		wgpu::DeviceDescriptor deviceDesc{};
		deviceDesc.requiredLimits = &limits;
		deviceDesc.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message)
			{
				ADD_FAILURE() << "WebGPU error: " << std::string_view(message.data, message.length);
			});
		s_instance.WaitAny(adapter.RequestDevice(&deviceDesc, wgpu::CallbackMode::WaitAnyOnly,
			[](wgpu::RequestDeviceStatus status, wgpu::Device result, wgpu::StringView)
			{
				if (status == wgpu::RequestDeviceStatus::Success)
					s_device = std::move(result);
			}), UINT64_MAX);
	}

	static void TearDownTestSuite()
	{
		s_device = nullptr;
		s_instance = nullptr;
	}

	void SetUp() override
	{
		if (s_device == nullptr)
			GTEST_SKIP() << "No D3D12 WebGPU device is available";
	}

	// Clears a target to `clear`, lights it and reads it back.
	static Image render(glm::vec4 clear, const ax::LightingSettings& lighting, std::span<const ax::Light2D> lights,
		std::span<const ax::Glow2D> glows = {}, std::uint32_t samples = 1, const ax::LightView& view = centred_view())
	{
		constexpr auto format{ wgpu::TextureFormat::RGBA8Unorm };
		const auto makeTexture = [](std::uint32_t sampleCount, wgpu::TextureUsage usage)
		{
			wgpu::TextureDescriptor desc{};
			desc.size = { s_size, s_size };
			desc.format = format;
			desc.sampleCount = sampleCount;
			desc.usage = usage;
			return s_device.CreateTexture(&desc);
		};
		const auto resolve{ makeTexture(1, wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc) };
		const auto target{ samples > 1 ? makeTexture(samples, wgpu::TextureUsage::RenderAttachment) : resolve };
		const auto targetView{ target.CreateView() };

		ax::LightRenderer renderer{};
		EXPECT_TRUE(renderer.init(s_device, format, samples));

		s_device.PushErrorScope(wgpu::ErrorFilter::Validation);
		auto encoder{ s_device.CreateCommandEncoder() };
		{
			wgpu::RenderPassColorAttachment colour{};
			colour.view = targetView;
			colour.loadOp = wgpu::LoadOp::Clear;
			colour.storeOp = wgpu::StoreOp::Store;
			colour.clearValue = { clear.r, clear.g, clear.b, clear.a };
			wgpu::RenderPassDescriptor passDesc{};
			passDesc.colorAttachmentCount = 1;
			passDesc.colorAttachments = &colour;
			encoder.BeginRenderPass(&passDesc).End();
		}
		renderer.encode(s_device, s_device.GetQueue(), encoder, targetView, s_size, s_size, view, lighting,
			lights, glows);
		if (samples > 1)
		{
			wgpu::RenderPassColorAttachment colour{};
			colour.view = targetView;
			colour.resolveTarget = resolve.CreateView();
			colour.loadOp = wgpu::LoadOp::Load;
			colour.storeOp = wgpu::StoreOp::Store;
			wgpu::RenderPassDescriptor passDesc{};
			passDesc.colorAttachmentCount = 1;
			passDesc.colorAttachments = &colour;
			encoder.BeginRenderPass(&passDesc).End();
		}

		constexpr std::uint32_t bytesPerRow{ s_size * 4 };
		wgpu::BufferDescriptor readbackDesc{};
		readbackDesc.size = bytesPerRow * s_size;
		readbackDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
		const auto readback{ s_device.CreateBuffer(&readbackDesc) };
		wgpu::TexelCopyTextureInfo source{};
		source.texture = resolve;
		wgpu::TexelCopyBufferInfo destination{};
		destination.buffer = readback;
		destination.layout.bytesPerRow = bytesPerRow;
		destination.layout.rowsPerImage = s_size;
		const wgpu::Extent3D extent{ s_size, s_size };
		encoder.CopyTextureToBuffer(&source, &destination, &extent);
		const auto commands{ encoder.Finish() };
		s_device.GetQueue().Submit(1, &commands);

		std::string validationError;
		s_instance.WaitAny(s_device.PopErrorScope(wgpu::CallbackMode::WaitAnyOnly,
			[&validationError](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView message)
			{
				if (type != wgpu::ErrorType::NoError)
					validationError = std::string(message.data, message.length);
			}), UINT64_MAX);
		EXPECT_TRUE(validationError.empty()) << validationError;

		bool mapped{ false };
		s_instance.WaitAny(readback.MapAsync(wgpu::MapMode::Read, 0, readbackDesc.size, wgpu::CallbackMode::WaitAnyOnly,
			[&mapped](wgpu::MapAsyncStatus status, wgpu::StringView)
			{
				mapped = status == wgpu::MapAsyncStatus::Success;
			}), UINT64_MAX);
		EXPECT_TRUE(mapped);
		Image image{ std::vector<std::uint8_t>(readbackDesc.size) };
		if (mapped)
		{
			std::memcpy(image.rgba.data(), readback.GetConstMappedRange(), readbackDesc.size);
			readback.Unmap();
		}
		return image;
	}

	static inline wgpu::Instance s_instance{};
	static inline wgpu::Device s_device{};
};

TEST(LightingActive, SkipsDisabledAndFullyLitFrames)
{
	ax::LightingSettings lighting{};
	EXPECT_FALSE(ax::lighting_active(lighting, 10, 10));
	lighting.enabled = true;
	EXPECT_FALSE(ax::lighting_active(lighting, 10, 0));
	EXPECT_TRUE(ax::lighting_active(lighting, 0, 1));
	lighting.maxExposure = 1.5f;
	EXPECT_TRUE(ax::lighting_active(lighting, 1, 0));
	EXPECT_FALSE(ax::lighting_active(lighting, 0, 0));
	lighting.maxExposure = 1.0f;
	lighting.ambient = { 1.0f, 0.9f, 1.0f };
	EXPECT_TRUE(ax::lighting_active(lighting, 0, 0));
}

TEST_F(LightRendererTest, AmbientMultipliesTarget)
{
	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, settings({ 0.5f, 0.25f, 1.0f }), {}) };
	EXPECT_TRUE(close_to(image.at(10, 10), { 128, 64, 255, 255 }, 2));
	EXPECT_TRUE(close_to(image.at(100, 90), { 128, 64, 255, 255 }, 2));
}

TEST_F(LightRendererTest, LightBrightensOnlyWithinItsRadius)
{
	const std::array lights{ ax::Light2D{ .position = { 0.0f, 0.0f }, .radius = 32.0f, .intensity = 2.0f,
		.colour = { 1.0f, 0.0f, 0.0f }, .falloff = 1.0f } };
	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, settings({ 0.1f, 0.1f, 0.1f }), lights) };
	EXPECT_TRUE(close_to(image.at(64, 64), { 255, 26, 26, 255 }, 3));
	EXPECT_GT(image.at(64 + 12, 64)[0], 200);
	EXPECT_TRUE(close_to(image.at(64 + 45, 64), { 26, 26, 26, 255 }, 3));
	EXPECT_TRUE(close_to(image.at(64, 64 - 45), { 26, 26, 26, 255 }, 3));
}

TEST_F(LightRendererTest, LightFollowsCamera)
{
	// The camera puts (100, 50) at the centre and doubles sizes, so the light covers 32 pixels each way.
	const std::array lights{ ax::Light2D{ .position = { 100.0f, 50.0f }, .radius = 16.0f, .intensity = 1.0f,
		.colour = { 0.0f, 1.0f, 0.0f }, .falloff = 1.0f } };
	auto view{ centred_view() };
	view.cameraPosition = { 100.0f, 50.0f };
	view.zoom = 2.0f;
	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, settings({ 0.0f, 0.0f, 0.0f }), lights, {}, 1, view) };
	EXPECT_GT(image.at(64, 64)[1], 240);
	EXPECT_EQ(image.at(64, 64)[0], 0);
	EXPECT_GT(image.at(64 + 20, 64)[1], 0);
	EXPECT_EQ(image.at(64 + 40, 64)[1], 0);
}

TEST_F(LightRendererTest, ConeOnlyLightsFacingDirection)
{
	const auto inner{ std::cos(std::numbers::pi_v<float> / 9.0f) };
	const auto outer{ std::cos(std::numbers::pi_v<float> / 6.0f) };
	const std::array lights{ ax::Light2D{ .position = { 0.0f, 0.0f }, .radius = 60.0f, .intensity = 1.0f,
		.colour = { 1.0f, 1.0f, 1.0f }, .falloff = 1.0f, .direction = { 1.0f, 0.0f }, .coneInner = inner,
		.coneOuter = outer } };
	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, settings({ 0.0f, 0.0f, 0.0f }), lights) };
	EXPECT_GT(image.at(64 + 20, 64)[0], 180);
	EXPECT_EQ(image.at(64 - 20, 64)[0], 0);
	EXPECT_EQ(image.at(64, 64 + 20)[0], 0);
	EXPECT_EQ(image.at(64 + 20, 64 + 20)[0], 0);
}

TEST_F(LightRendererTest, DimAmbientKeepsFullPrecision)
{
	// At an exposure of 1 the light is applied as is: 0.12 of white is 31, not the 30 a halved light would round to.
	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, settings({ 0.12f, 0.12f, 0.12f }), {}) };
	EXPECT_TRUE(close_to(image.at(10, 10), { 31, 31, 31, 255 }, 0));
}

TEST_F(LightRendererTest, ExposureIsClamped)
{
	const std::array lights{ ax::Light2D{ .position = { 0.0f, 0.0f }, .radius = 60.0f, .intensity = 10.0f,
		.colour = { 1.0f, 1.0f, 1.0f }, .falloff = 1.0f } };
	const auto image{ render({ 0.5f, 0.5f, 0.5f, 1.0f }, settings({ 0.0f, 0.0f, 0.0f }, 1.5f), lights) };
	EXPECT_TRUE(close_to(image.at(64, 64), { 191, 191, 191, 255 }, 3));
}

TEST_F(LightRendererTest, GlowsAddOverTheLitScene)
{
	const std::array glows{ ax::Glow2D{ .position = { 0.0f, 0.0f }, .radius = 20.0f, .intensity = 1.0f,
		.colour = { 0.0f, 1.0f, 0.0f, 1.0f } } };
	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, settings({ 0.0f, 0.0f, 0.0f }), {}, glows) };
	EXPECT_GT(image.at(64, 64)[1], 240);
	EXPECT_EQ(image.at(64, 64)[0], 0);
	EXPECT_TRUE(close_to(image.at(64 + 30, 64), { 0, 0, 0, 255 }, 0));
}

TEST_F(LightRendererTest, ManyLightsAccumulateAdditively)
{
	std::vector<ax::Light2D> lights(1000, ax::Light2D{ .position = { 0.0f, 0.0f }, .radius = 60.0f,
		.intensity = 0.0005f, .colour = { 1.0f, 1.0f, 1.0f }, .falloff = 1.0f });
	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, settings({ 0.0f, 0.0f, 0.0f }), lights) };
	EXPECT_TRUE(close_to(image.at(64, 64), { 128, 128, 128, 255 }, 4));
}

TEST_F(LightRendererTest, DrawsOntoMultisampledTargets)
{
	const std::array lights{ ax::Light2D{ .position = { 0.0f, 0.0f }, .radius = 32.0f, .intensity = 1.0f,
		.colour = { 0.0f, 0.0f, 1.0f }, .falloff = 1.0f } };
	const std::array glows{ ax::Glow2D{ .position = { 40.0f, 40.0f }, .radius = 10.0f } };
	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, settings({ 0.5f, 0.5f, 0.5f }), lights, glows, 4) };
	EXPECT_TRUE(close_to(image.at(64, 64), { 128, 128, 255, 255 }, 3));
	EXPECT_TRUE(close_to(image.at(4, 4), { 128, 128, 128, 255 }, 3));
}

// Run with --gtest_also_run_disabled_tests to time the lighting passes at 1080p with 4x MSAA.
TEST_F(LightRendererTest, DISABLED_ReportsGpuTimeForManyLights)
{
	constexpr std::uint32_t width{ 1920 }, height{ 1080 };
	constexpr auto format{ wgpu::TextureFormat::RGBA8Unorm };
	wgpu::TextureDescriptor desc{};
	desc.size = { width, height };
	desc.format = format;
	desc.sampleCount = 4;
	desc.usage = wgpu::TextureUsage::RenderAttachment;
	const auto target{ s_device.CreateTexture(&desc) };
	const auto view{ target.CreateView() };
	ax::LightRenderer renderer{};
	ASSERT_TRUE(renderer.init(s_device, format, 4));
	const ax::LightView camera{ .viewport = { static_cast<float>(width), static_cast<float>(height) }, .zoom = 0.6f };

	for (const auto count : { 1000u, 5000u, 20000u })
		for (const auto divisor : { 1u, 2u, 4u })
		{
			std::vector<ax::Light2D> lights{};
			std::vector<ax::Glow2D> glows{};
			for (std::uint32_t index{}; index < count; ++index)
			{
				// Lamps 10 tiles across, spread over the view at a lamp every few tiles.
				const glm::vec2 position{ static_cast<float>((index * 7919u) % 3200u) - 1600.0f,
					static_cast<float>((index * 104729u) % 1800u) - 900.0f };
				lights.push_back({ .position = position, .radius = 320.0f, .intensity = 0.5f, .colour = { 1.0f, 0.9f, 0.7f },
					.falloff = 1.6f });
				glows.push_back({ .position = position, .radius = 35.0f, .intensity = 0.55f });
			}
			const auto settings{ ax::LightingSettings{ .enabled = true, .ambient = glm::vec3{ 0.12f },
				.resolutionDivisor = divisor } };
			const auto frame{ [&]
			{
				auto encoder{ s_device.CreateCommandEncoder() };
				renderer.encode(s_device, s_device.GetQueue(), encoder, view, width, height, camera, settings, lights, glows);
				const auto commands{ encoder.Finish() };
				s_device.GetQueue().Submit(1, &commands);
				s_instance.WaitAny(s_device.GetQueue().OnSubmittedWorkDone(wgpu::CallbackMode::WaitAnyOnly,
					[](wgpu::QueueWorkDoneStatus, wgpu::StringView) {}), UINT64_MAX);
			} };
			frame();
			constexpr int frames{ 30 };
			const auto started{ std::chrono::steady_clock::now() };
			for (int index{}; index < frames; ++index) frame();
			const auto elapsed{ std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() };
			std::printf("%u lights + glows, light map 1/%u: %.3f ms per frame\n", count, divisor, elapsed / frames);
		}
}
