#include <gtest/gtest.h>
#include <dawn/native/DawnNative.h>
#include <windows.h>

#include "axeng/core/lighting/light_renderer.h"
#include "axeng/core/particles/particle_system.h"
#include "axeng/core/texture.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
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

		// AXENG_TEST_BACKEND=vulkan runs these on Vulkan instead of D3D12 (to compare their GPU times).
		char* backendText{};
		std::size_t backendLength{};
		_dupenv_s(&backendText, &backendLength, "AXENG_TEST_BACKEND");
		const bool vulkan{ backendText && std::string_view{ backendText } == "vulkan" };
		std::free(backendText);
		wgpu::RequestAdapterOptions options{};
		options.backendType = vulkan ? wgpu::BackendType::Vulkan : wgpu::BackendType::D3D12;
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
		// As the engine does: GPU timings where the adapter has timestamp queries, not rounded.
		static const char* s_disabled_toggles[]{ "timestamp_quantization" };
		wgpu::DawnTogglesDescriptor togglesDesc{};
		togglesDesc.disabledToggleCount = 1;
		togglesDesc.disabledToggles = s_disabled_toggles;
		deviceDesc.nextInChain = &togglesDesc;
		static const auto s_timestamp_query{ wgpu::FeatureName::TimestampQuery };
		if (adapter.HasFeature(s_timestamp_query))
		{
			deviceDesc.requiredFeatureCount = 1;
			deviceDesc.requiredFeatures = &s_timestamp_query;
		}
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
		std::span<const ax::Glow2D> glows = {}, std::uint32_t samples = 1, const ax::LightView& view = centred_view(),
		const std::function<void(wgpu::CommandEncoder&)>& before = {},
		const std::function<void(wgpu::RenderPassEncoder&, std::uint32_t)>& extra = {}, const ax::NoiseShadow2D* noiseShadow = nullptr,
		std::span<const ax::LightOccluder2D> occluders = {}, std::span<const ax::RadianceCircle> circles = {},
		ax::LightRenderer::Stats* stats = nullptr, const ax::RadianceCascades::OccluderDraw& radianceOccluders = {},
		int frames = 1)
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
		if (before)
			before(encoder);
		// Earlier frames only leave their light map behind (for bounce light). They draw into a throwaway target: the
		// light map multiplies whatever is beneath, so drawing them into the real one would apply the light twice.
		const auto scratch{ frames > 1 ? makeTexture(samples, wgpu::TextureUsage::RenderAttachment) : wgpu::Texture{} };
		const auto scratchView{ frames > 1 ? scratch.CreateView() : wgpu::TextureView{} };
		for (int frame{ 1 }; frame < frames; ++frame)
		{
			ax::LightingSettings earlier{ lighting };
			renderer.encode(s_device, s_device.GetQueue(), encoder, scratchView, s_size, s_size, view, earlier,
				lights, glows, extra, noiseShadow, occluders, circles, radianceOccluders);
		}
		renderer.encode(s_device, s_device.GetQueue(), encoder, targetView, s_size, s_size, view, lighting,
			lights, glows, extra, noiseShadow, occluders, circles, radianceOccluders);
		if (stats)
			*stats = renderer.last_stats();
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
	EXPECT_TRUE(ax::lighting_active(lighting, 10, 0, true));
	EXPECT_TRUE(ax::lighting_active(lighting, 0, 1));
	lighting.maxExposure = 1.5f;
	EXPECT_TRUE(ax::lighting_active(lighting, 1, 0));
	EXPECT_FALSE(ax::lighting_active(lighting, 0, 0));
	lighting.maxExposure = 1.0f;
	lighting.ambient = { 1.0f, 0.9f, 1.0f };
	EXPECT_TRUE(ax::lighting_active(lighting, 0, 0));
}

TEST(Tonemap, DefaultClampsAtMaxExposure)
{
	const ax::Tonemap tonemap{};
	EXPECT_EQ(ax::tonemap_light(tonemap, { 0.12f, 0.5f, 1.0f }, 1.0f), glm::vec3(0.12f, 0.5f, 1.0f));
	const auto clipped{ ax::tonemap_light(tonemap, { 3.0f, 1.2f, -1.0f }, 1.5f) };
	EXPECT_NEAR(clipped.r, 1.5f, 1e-5f);
	EXPECT_NEAR(clipped.g, 1.2f, 1e-5f);
	EXPECT_EQ(clipped.b, 0.0f);
}

TEST(Tonemap, CurvesReachMaxExposureAtWhiteAndRollOffBelowIt)
{
	using Curve = ax::Tonemap::Curve;
	for (const auto curve : { Curve::reinhard, Curve::aces, Curve::filmic })
	{
		const ax::Tonemap tonemap{ .curve = curve, .white = 4.0f };
		EXPECT_NEAR(ax::tonemap_light(tonemap, glm::vec3{ 4.0f }, 1.0f).r, 1.0f, 1e-4f);
		EXPECT_NEAR(ax::tonemap_light(tonemap, glm::vec3{ 9.0f }, 1.2f).r, 1.2f, 1e-4f);
		const float half{ ax::tonemap_light(tonemap, glm::vec3{ 1.0f }, 1.0f).r };
		EXPECT_GT(half, 0.0f);
		EXPECT_LT(half, 1.0f);
		EXPECT_LT(ax::tonemap_light(tonemap, glm::vec3{ 0.5f }, 1.0f).r, half);
	}
}

TEST(Tonemap, ExposureSaturationGammaAndHue)
{
	EXPECT_NEAR(ax::tonemap_light({ .exposure = 0.5f }, glm::vec3{ 0.8f }, 1.0f).r, 0.4f, 1e-5f);
	const auto grey{ ax::tonemap_light({ .saturation = 0.0f }, { 1.0f, 0.0f, 0.0f }, 1.0f) };
	EXPECT_NEAR(grey.r, grey.g, 1e-5f);
	EXPECT_NEAR(ax::tonemap_light({ .gamma = 2.0f }, glm::vec3{ 0.5f }, 1.0f).r, 0.25f, 1e-5f);
	// Bright orange light: clipped per channel it goes yellow; with the hue kept it stays orange.
	const glm::vec3 orange{ 4.0f, 2.0f, 0.0f };
	const auto clipped{ ax::tonemap_light({}, orange, 1.0f) };
	EXPECT_EQ(clipped.g, 1.0f);
	const auto kept{ ax::tonemap_light({ .preserveHue = true }, orange, 1.0f) };
	EXPECT_NEAR(kept.r, 1.0f, 1e-5f);
	EXPECT_NEAR(kept.g, 0.5f, 1e-5f);
}

TEST(LightingActive, RunsForATonemapThatChangesFullLight)
{
	ax::LightingSettings lighting{ .enabled = true };
	lighting.tonemap.exposure = 1.0f;
	EXPECT_FALSE(ax::lighting_active(lighting, 0, 0));
	lighting.tonemap.curve = ax::Tonemap::Curve::reinhard; // white 1: full light stays 1
	EXPECT_FALSE(ax::lighting_active(lighting, 0, 0));
	lighting.tonemap.white = 3.0f;
	EXPECT_TRUE(ax::lighting_active(lighting, 0, 0));
}

TEST_F(LightRendererTest, ReinhardTonemapRollsOffABrightLight)
{
	const std::array lights{ ax::Light2D{ .position = { 0.0f, 0.0f }, .radius = 60.0f, .intensity = 10.0f,
		.colour = { 1.0f, 1.0f, 1.0f }, .falloff = 1.0f } };
	auto lighting{ settings({ 0.5f, 0.5f, 0.5f }) };
	lighting.tonemap = { .curve = ax::Tonemap::Curve::reinhard, .white = 100.0f };
	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, lights) };
	// Ambient 0.5 maps to 0.5 / 1.5 of white (with white far above it).
	const auto expected{ static_cast<int>(std::lround(255.0f * ax::tonemap_light(lighting.tonemap, glm::vec3{ 0.5f }, 1.0f).r)) };
	EXPECT_TRUE(close_to(image.at(4, 4), { expected, expected, expected, 255 }, 2));
	EXPECT_LT(image.at(64, 64)[0], 255);
	EXPECT_GT(image.at(64, 64)[0], image.at(4, 4)[0]);
}

TEST_F(LightRendererTest, AmbientMultipliesTarget)
{
	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, settings({ 0.5f, 0.25f, 1.0f }), {}) };
	EXPECT_TRUE(close_to(image.at(10, 10), { 128, 64, 255, 255 }, 2));
	EXPECT_TRUE(close_to(image.at(100, 90), { 128, 64, 255, 255 }, 2));
}

TEST_F(LightRendererTest, NegativeLightCastsAShadowOnFullyLitAmbient)
{
	const std::array lights{ ax::Light2D{ .position = { 0.0f, 0.0f }, .radius = 40.0f, .intensity = -0.5f,
		.colour = { 1.0f, 1.0f, 1.0f }, .falloff = 2.0f } };
	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, settings({ 1.0f, 1.0f, 1.0f }), lights) };
	EXPECT_LT(image.at(64, 64)[0], 255);
	EXPECT_TRUE(close_to(image.at(10, 10), { 255, 255, 255, 255 }, 2));
}

TEST_F(LightRendererTest, NoiseShadowDarkensLightMapAcrossTheScene)
{
	const ax::NoiseShadow2D shadow{ .scale = { 36.0f, 36.0f }, .coverage = 0.45f, .softness = 0.12f,
		.darkness = 0.35f, .seed = 17.0f };
	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, settings({ 1.0f, 1.0f, 1.0f }), {}, {}, 1,
		centred_view(), {}, {}, &shadow) };
	int darkest{ 255 };
	int brightest{};
	for (std::uint32_t y = 8; y < s_size - 8; ++y)
		for (std::uint32_t x = 8; x < s_size - 8; ++x)
		{
			const auto value{ image.at(x, y)[0] };
			darkest = std::min(darkest, value);
			brightest = std::max(brightest, value);
		}
	EXPECT_LT(darkest, 230);
	EXPECT_GT(brightest, 245);
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

TEST_F(LightRendererTest, OccluderHidesOnlyTheLightsBeforeIt)
{
	// A red light at the centre, then a deck over the left half (turned a little), then a green light on the deck.
	const std::array lights{
		ax::Light2D{ .position = { 0.0f, 0.0f }, .radius = 48.0f, .intensity = 2.0f, .colour = { 1.0f, 0.0f, 0.0f },
			.falloff = 1.0f },
		ax::Light2D{ .position = { -30.0f, 0.0f }, .radius = 16.0f, .intensity = 2.0f, .colour = { 0.0f, 1.0f, 0.0f },
			.falloff = 1.0f } };
	const std::array occluders{ ax::LightOccluder2D{
		.corners = { glm::vec2{ -70.0f, -70.0f }, glm::vec2{ -4.0f, -66.0f }, glm::vec2{ -8.0f, 70.0f },
			glm::vec2{ -70.0f, 70.0f } },
		.firstLight = 1 } };
	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, settings({ 0.1f, 0.1f, 0.1f }), lights, {}, 1, centred_view(),
		{}, {}, nullptr, occluders) };
	// Beside the deck the red light shines; on it there is only ambient and the green light.
	EXPECT_GT(image.at(64 + 12, 64)[0], 200);
	EXPECT_TRUE(close_to(image.at(64 - 20, 64 - 30), { 26, 26, 26, 255 }, 3));
	EXPECT_TRUE(close_to(image.at(64 - 30, 64), { 26, 255, 26, 255 }, 3));
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

TEST_F(LightRendererTest, BeamLightsAlongItsLineAndFadesAcrossIt)
{
	// A beam from (-40, 0) to (40, 0), lighting 12 pixels either side of the line.
	const std::array lights{ ax::Light2D{ .position = { -40.0f, 0.0f }, .radius = 12.0f, .intensity = 1.0f,
		.colour = { 1.0f, 1.0f, 1.0f }, .falloff = 1.0f, .direction = { 1.0f, 0.0f }, .length = 80.0f } };
	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, settings({ 0.0f, 0.0f, 0.0f }), lights) };
	// Fully lit all along the line, the same at its middle as near its ends.
	EXPECT_GT(image.at(64, 64)[0], 240);
	EXPECT_GT(image.at(64 - 34, 64)[0], 240);
	EXPECT_GT(image.at(64 + 34, 64)[0], 240);
	// Dimmer off to the side, dark past its radius, and dark well beyond its ends.
	EXPECT_LT(image.at(64, 64 + 7)[0], 200);
	EXPECT_GT(image.at(64, 64 + 7)[0], 20);
	EXPECT_EQ(image.at(64, 64 + 16)[0], 0);
	EXPECT_EQ(image.at(64 - 56, 64)[0], 0);
	EXPECT_EQ(image.at(64 + 56, 64)[0], 0);
	// Round at the ends: just past an end it is still lit.
	EXPECT_GT(image.at(64 + 44, 64)[0], 20);
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

TEST_F(LightRendererTest, ParticlesLightTheSceneAroundThem)
{
	ax::ParticleSystem particles;
	ax::ParticleConfig config{};
	config.capacity = 4;
	config.emissionRate = 1;
	config.duration = 1.0f;
	config.explosiveness = 1.0f;
	config.lifetimeMin = 10.0f;
	config.lifetimeMax = 10.0f;
	config.speedMin = 0.0f;
	config.speedMax = 0.0f;
	config.sizeStart = 4.0f;
	config.sizeEnd = 4.0f;
	config.spawnRadius = 0.0f;
	config.colorStart = { 0.0f, 1.0f, 0.0f, 1.0f };
	config.colorEnd = { 0.0f, 1.0f, 0.0f, 1.0f };
	config.particleLight = { .enabled = true, .radiusScale = 8.0f, .intensity = 1.0f, .falloff = 1.0f };
	config.position = { 0.0f, 0.0f };
	const auto emitter{ particles.create(config) };
	emitter->play();
	// A particle just off screen still lights the edge of the view: it is 80 from the centre of a 128 wide view, with
	// a light radius of 32.
	auto offscreen{ config };
	offscreen.position = { 80.0f, 0.0f };
	offscreen.colorStart = offscreen.colorEnd = { 1.0f, 0.0f, 0.0f, 1.0f };
	const auto offscreenEmitter{ particles.create(offscreen) };
	offscreenEmitter->play();

	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, settings({ 0.0f, 0.0f, 0.0f }), {}, {}, 1, centred_view(),
		[&](wgpu::CommandEncoder& encoder)
		{
			auto queue{ s_device.GetQueue() };
			particles.encode_compute(s_device, queue, encoder, 0.1, { -64.0f, -64.0f, 64.0f, 64.0f },
				{ static_cast<float>(s_size), static_cast<float>(s_size) }, 1.0f, wgpu::TextureFormat::RGBA8Unorm, 1);
		},
		[&](wgpu::RenderPassEncoder& pass, std::uint32_t) { particles.draw_lights(pass); }) };
	EXPECT_EQ(particles.emission_light_emitters(), 2u);
	EXPECT_GT(image.at(64, 64)[1], 240);
	EXPECT_EQ(image.at(64, 64)[0], 0);
	EXPECT_GT(image.at(64 + 15, 64)[1], 0);
	EXPECT_EQ(image.at(64 + 40, 64)[1], 0);
	EXPECT_GT(image.at(127, 64)[0], 0) << "an off screen particle's light reaches into the view";
	EXPECT_EQ(image.at(64, 64 + 40)[1], 0);
	particles.shutdown();
}

TEST_F(LightRendererTest, ParticleLightsGoBeneathOccluders)
{
	// A deck over a glowing particle hides its light; a light added after the deck still shows.
	ax::ParticleSystem particles;
	ax::ParticleConfig config{};
	config.capacity = 4;
	config.emissionRate = 1;
	config.duration = 1.0f;
	config.explosiveness = 1.0f;
	config.lifetimeMin = 10.0f;
	config.lifetimeMax = 10.0f;
	config.speedMin = 0.0f;
	config.speedMax = 0.0f;
	config.spawnRadius = 0.0f;
	config.colorStart = config.colorEnd = { 0.0f, 1.0f, 0.0f, 1.0f };
	config.particleLight = { .enabled = true, .radiusScale = 8.0f, .intensity = 1.0f, .falloff = 1.0f };
	const auto emitter{ particles.create(config) };
	emitter->play();
	const std::array lights{ ax::Light2D{ .position = { 30.0f, 30.0f }, .radius = 16.0f, .intensity = 2.0f,
		.colour = { 1.0f, 0.0f, 0.0f }, .falloff = 1.0f } };
	const std::array occluders{ ax::LightOccluder2D{
		.corners = { glm::vec2{ -40.0f, -40.0f }, glm::vec2{ 40.0f, -40.0f }, glm::vec2{ 40.0f, 40.0f },
			glm::vec2{ -40.0f, 40.0f } },
		.firstLight = 0 } };
	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, settings({ 0.0f, 0.0f, 0.0f }), lights, {}, 1, centred_view(),
		[&](wgpu::CommandEncoder& encoder)
		{
			auto queue{ s_device.GetQueue() };
			particles.encode_compute(s_device, queue, encoder, 0.1, { -64.0f, -64.0f, 64.0f, 64.0f },
				{ static_cast<float>(s_size), static_cast<float>(s_size) }, 1.0f, wgpu::TextureFormat::RGBA8Unorm, 1);
		},
		[&](wgpu::RenderPassEncoder& pass, std::uint32_t layer)
		{
			particles.draw_lights(pass, [layer](std::uint32_t emitter) { return emitter == layer; });
		}, nullptr, occluders) };
	EXPECT_EQ(particles.emission_light_emitters(), 1u);
	EXPECT_EQ(image.at(64, 64)[1], 0) << "the particle's light is hidden beneath the deck";
	EXPECT_GT(image.at(64 + 30, 64 + 30)[0], 200) << "a light added after the deck shows on it";
	particles.shutdown();
}

TEST_F(LightRendererTest, ParticleLightsInAnOccludersLayerShowOnIt)
{
	// A particle on the deck (in the deck's light layer) lights the deck; one in layer 0 beside it, under the deck, does not.
	ax::ParticleSystem particles;
	ax::ParticleConfig config{};
	config.capacity = 4;
	config.emissionRate = 1;
	config.duration = 1.0f;
	config.explosiveness = 1.0f;
	config.lifetimeMin = 10.0f;
	config.lifetimeMax = 10.0f;
	config.speedMin = 0.0f;
	config.speedMax = 0.0f;
	config.spawnRadius = 0.0f;
	config.colorStart = config.colorEnd = { 0.0f, 1.0f, 0.0f, 1.0f };
	config.particleLight = { .enabled = true, .radiusScale = 4.0f, .intensity = 1.0f, .falloff = 1.0f };
	config.position = { -20.0f, 0.0f };
	config.lightLayer = 7;
	const auto onDeck{ particles.create(config) };
	onDeck->play();
	auto beneath{ config };
	beneath.position = { 20.0f, 0.0f };
	beneath.lightLayer = 0;
	const auto underDeck{ particles.create(beneath) };
	underDeck->play();
	const std::array occluders{ ax::LightOccluder2D{
		.corners = { glm::vec2{ -40.0f, -40.0f }, glm::vec2{ 40.0f, -40.0f }, glm::vec2{ 40.0f, 40.0f },
			glm::vec2{ -40.0f, 40.0f } },
		.firstLight = 0, .layer = 7 } };
	std::vector<std::uint32_t> calls{};
	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, settings({ 0.0f, 0.0f, 0.0f }), {}, {}, 1, centred_view(),
		[&](wgpu::CommandEncoder& encoder)
		{
			auto queue{ s_device.GetQueue() };
			particles.encode_compute(s_device, queue, encoder, 0.1, { -64.0f, -64.0f, 64.0f, 64.0f },
				{ static_cast<float>(s_size), static_cast<float>(s_size) }, 1.0f, wgpu::TextureFormat::RGBA8Unorm, 1);
		},
		[&](wgpu::RenderPassEncoder& pass, std::uint32_t layer)
		{
			calls.push_back(layer);
			particles.draw_lights(pass, [layer](std::uint32_t emitter) { return emitter == layer; });
		}, nullptr, occluders) };
	EXPECT_EQ(calls, (std::vector<std::uint32_t>{ 0, 7 }));
	EXPECT_GT(image.at(64 - 20, 64)[1], 200) << "the deck's own particle lights it";
	EXPECT_EQ(image.at(64 + 20, 64)[1], 0) << "a particle beneath the deck does not";
	particles.shutdown();
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

namespace
{
	ax::LightingSettings radiance_settings()
	{
		auto lighting{ settings(glm::vec3{ 0.0f }) };
		lighting.resolutionDivisor = 1;
		lighting.radiance = { .enabled = true, .divisor = 1, .probeSpacing = 1 };
		return lighting;
	}

	// Screen pixel of a world position in the centred view.
	std::uint32_t at(float world) { return static_cast<std::uint32_t>(static_cast<float>(s_size) * 0.5f + world); }
}

TEST(RadianceGrid, CoversTheViewAndMarginSnappedToTheCoarsestProbes)
{
	const ax::LightView view{ .viewport = { 200.0f, 100.0f }, .cameraPosition = { 13.0f, -7.0f }, .zoom = 2.0f };
	const ax::RadianceSettings radiance{ .enabled = true, .divisor = 2, .probeSpacing = 1, .margin = 20.0f };
	const auto grid{ ax::radiance_grid(view, radiance, 100.0f) };
	EXPECT_FLOAT_EQ(grid.texelsPerPixel, 1.0f);
	// 100 texels of reach: four cascades reach (4^4 - 1) / 3 = 85 texels, five reach 341.
	EXPECT_EQ(grid.cascades, 5u);
	const auto snap{ static_cast<float>(1u << (grid.cascades - 1)) };
	EXPECT_FLOAT_EQ(std::fmod(grid.origin.x, snap), 0.0f);
	EXPECT_FLOAT_EQ(std::fmod(grid.origin.y, snap), 0.0f);
	EXPECT_EQ(grid.width % 16u, 0u);
	EXPECT_EQ(grid.height % 16u, 0u);
	EXPECT_LE(grid.world_min().x, 13.0f - 50.0f - 20.0f);
	EXPECT_LE(grid.world_min().y, -7.0f - 25.0f - 20.0f);
	EXPECT_GE(grid.world_max().x, 13.0f + 50.0f + 20.0f);
	EXPECT_GE(grid.world_max().y, -7.0f + 25.0f + 20.0f);
	// Moving the camera by less than a snap keeps the origin.
	auto moved{ view };
	moved.cameraPosition.x += 0.25f;
	EXPECT_EQ(ax::radiance_grid(moved, radiance, 100.0f).origin, grid.origin);
}

TEST(RadianceGrid, TexelsStayFixedInTheWorldWhileZooming)
{
	// Within an octave of zoom the grid's texels (and so its probes) keep their size in the world, so zooming does not
	// shift them all and the light sampled at them.
	const ax::RadianceSettings radiance{ .enabled = true, .divisor = 2, .probeSpacing = 2, .margin = 0.0f };
	const auto at{ [&](float zoom) { return ax::radiance_grid({ .viewport = { 400.0f, 300.0f }, .zoom = zoom }, radiance,
		100.0f).texelsPerPixel; } };
	EXPECT_FLOAT_EQ(at(1.5f), std::sqrt(0.5f));
	EXPECT_FLOAT_EQ(at(1.25f), at(1.6f));
	EXPECT_FLOAT_EQ(at(2.0f), 1.0f);
	EXPECT_FLOAT_EQ(at(1.75f), at(2.3f));
}

TEST(RadianceGrid, TracesOnlyShadowCastingLightsInside)
{
	const ax::LightView view{ .viewport = { 128.0f, 128.0f } };
	const ax::RadianceSettings radiance{ .enabled = true, .divisor = 1, .margin = 0.0f };
	const auto grid{ ax::radiance_grid(view, radiance, 64.0f) };
	ax::Light2D light{ .position = { 0.0f, 0.0f }, .radius = 40.0f };
	EXPECT_TRUE(ax::radiance_traces(light, grid, radiance));
	auto noShadows{ light };
	noShadows.shadows = 0.0f;
	EXPECT_FALSE(ax::radiance_traces(noShadows, grid, radiance));
	auto beam{ light };
	beam.length = 20.0f;
	EXPECT_FALSE(ax::radiance_traces(beam, grid, radiance));
	auto tiny{ light };
	tiny.radius = 3.0f;
	EXPECT_FALSE(ax::radiance_traces(tiny, grid, radiance));
	auto outside{ light };
	outside.position = { 1000.0f, 0.0f };
	EXPECT_FALSE(ax::radiance_traces(outside, grid, radiance));
	auto off{ radiance };
	off.enabled = false;
	EXPECT_FALSE(ax::radiance_traces(light, grid, off));
}

TEST_F(LightRendererTest, RadianceMatchesTheUnoccludedLight)
{
	const std::array lights{ ax::Light2D{ .position = { 0.0f, 0.0f }, .radius = 48.0f, .intensity = 1.0f } };
	auto plain{ radiance_settings() };
	plain.radiance.enabled = false;
	const auto analytic{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, plain, lights) };
	for (const auto spacing : { 1u, 2u })
	{
		auto lighting{ radiance_settings() };
		lighting.radiance.probeSpacing = spacing;
		ax::LightRenderer::Stats stats{};
		const auto traced{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, lights, {}, 1, centred_view(), {}, {},
			nullptr, {}, {}, &stats) };
		EXPECT_EQ(stats.radianceLights, 1u);
		EXPECT_EQ(stats.lights, 1u);
		EXPECT_GT(stats.radianceCascades, 1u);
		// Coarser probes resolve a little less precisely.
		const int tolerance{ spacing == 1 ? 24 : 40 };
		for (const auto distance : { 6.0f, 14.0f, 24.0f, 34.0f })
		{
			EXPECT_TRUE(close_to(traced.at(at(distance), at(0.0f)), analytic.at(at(distance), at(0.0f)), tolerance))
				<< distance << " spacing " << spacing;
			EXPECT_TRUE(close_to(traced.at(at(0.0f), at(-distance)), analytic.at(at(0.0f), at(-distance)), tolerance))
				<< distance << " spacing " << spacing;
			const auto diagonal{ distance * 0.7071f };
			EXPECT_TRUE(close_to(traced.at(at(diagonal), at(diagonal)), analytic.at(at(diagonal), at(diagonal)), tolerance))
				<< distance << " spacing " << spacing;
		}
		EXPECT_TRUE(close_to(traced.at(at(54.0f), at(0.0f)), { 0, 0, 0, 255 }, 6));
	}
}

TEST_F(LightRendererTest, RadianceAddsLightsSharingOneSource)
{
	// A lamp and a cone facing +x from the same point: the cone's disc covers the lamp's, yet both still shine.
	const std::array lights{
		ax::Light2D{ .position = { 0.0f, 0.0f }, .radius = 36.0f, .intensity = 0.6f, .sourceRadius = 3.0f },
		ax::Light2D{ .position = { 0.0f, 0.0f }, .radius = 56.0f, .intensity = 0.6f, .direction = { 1.0f, 0.0f },
			.coneInner = std::cos(0.3f), .coneOuter = std::cos(0.55f), .sourceRadius = 3.0f } };
	auto plain{ radiance_settings() };
	plain.radiance.enabled = false;
	const auto analytic{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, plain, lights) };
	ax::LightRenderer::Stats stats{};
	const auto traced{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, radiance_settings(), lights, {}, 1, centred_view(), {}, {},
		nullptr, {}, {}, &stats) };
	EXPECT_EQ(stats.radianceLights, 2u);
	// Behind the cone only the lamp lights; ahead, both.
	for (const auto distance : { 10.0f, 20.0f })
	{
		EXPECT_GT(traced.at(at(-distance), at(0.0f))[0], 20) << distance;
		EXPECT_TRUE(close_to(traced.at(at(-distance), at(0.0f)), analytic.at(at(-distance), at(0.0f)), 24)) << distance;
		EXPECT_TRUE(close_to(traced.at(at(distance), at(0.0f)), analytic.at(at(distance), at(0.0f)), 24)) << distance;
	}
}

TEST_F(LightRendererTest, RadianceOccluderEdgesAreSmooth)
{
	// A light beside a round occluder: across its edge and inside it the light changes smoothly, without pixels much
	// darker than their neighbours (stripes inside, speckles where rays skim its side, a dark rim round it).
	const std::array lights{ ax::Light2D{ .position = { -50.0f, -20.0f }, .radius = 120.0f, .intensity = 1.0f,
		.falloff = 1.0f } };
	const std::array circles{ ax::RadianceCircle{ .centre = { 0.0f, 0.0f }, .radius = 22.0f } };
	for (const auto spacing : { 1u, 2u })
	{
		auto lighting{ radiance_settings() };
		lighting.radiance.divisor = 2;
		lighting.radiance.probeSpacing = spacing;
		const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, lights, {}, 1, centred_view(), {}, {}, nullptr, {},
			circles) };
		int dips{};
		for (std::uint32_t y{ 1 }; y + 1 < s_size; ++y)
			for (std::uint32_t x{ 1 }; x + 1 < s_size; ++x)
			{
				const auto dx{ static_cast<float>(x) - 64.0f }, dy{ static_cast<float>(y) - 64.0f };
				if (std::hypot(dx, dy) < 15.0f || std::hypot(dx, dy) > 30.0f) continue;
				std::array neighbours{ image.at(x + 1, y)[0], image.at(x - 1, y)[0], image.at(x, y + 1)[0],
					image.at(x, y - 1)[0] };
				std::sort(neighbours.begin(), neighbours.end());
				dips += neighbours[2] - image.at(x, y)[0] > 40 ? 1 : 0;
			}
		EXPECT_EQ(dips, 0) << "spacing " << spacing;
	}
}

TEST_F(LightRendererTest, RadianceMovingLightDoesNotFlicker)
{
	// The player's torch seen zoomed out at Medium, moved a fraction of a GI texel each frame: the light changes about as
	// smoothly as the analytic light does, not in crawling rings as rays catch or miss the light's texels.
	ax::LightView view{ centred_view() };
	view.zoom = 0.25f;
	auto lighting{ radiance_settings() };
	lighting.radiance.probeSpacing = 2;
	auto plain{ lighting };
	plain.radiance.enabled = false;
	std::vector<Image> traced{}, analytic{};
	for (int frame{}; frame < 6; ++frame)
	{
		const glm::vec2 at{ -100.0f + static_cast<float>(frame) * 0.7f, 10.0f + static_cast<float>(frame) * 0.3f };
		const std::array lights{ ax::Light2D{ .position = at, .radius = 9.0f * 64.0f, .intensity = 0.85f, .falloff = 1.2f,
			.direction = { 1.0f, 0.0f }, .coneInner = std::cos(0.30f), .coneOuter = std::cos(0.55f),
			.sourceRadius = 0.3f * 64.0f } };
		traced.push_back(render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, lights, {}, 1, view));
		analytic.push_back(render({ 1.0f, 1.0f, 1.0f, 1.0f }, plain, lights, {}, 1, view));
	}
	double excess{};
	int counted{};
	for (std::size_t frame{ 1 }; frame < traced.size(); ++frame)
		for (std::uint32_t y{}; y < s_size; ++y)
			for (std::uint32_t x{}; x < s_size; ++x, ++counted)
				excess += std::max(0, std::abs(traced[frame].at(x, y)[0] - traced[frame - 1].at(x, y)[0])
					- std::abs(analytic[frame].at(x, y)[0] - analytic[frame - 1].at(x, y)[0]));
	EXPECT_LT(excess / counted, 0.35);
}

TEST_F(LightRendererTest, RadianceLightNearThePlayerDoesNotFlicker)
{
	// Zoomed in at Medium with the camera on the player as it walks a fraction of a pixel each frame: right round its
	// lamp, where the cascades' few nearby rays cannot resolve the source, the light stays steady.
	auto lighting{ radiance_settings() };
	lighting.resolutionDivisor = 2;
	lighting.radiance = { .enabled = true, .divisor = 2, .probeSpacing = 2, .margin = 640.0f, .maxMarginFraction = 0.3f,
		.bounce = 0.25f };
	auto plain{ lighting };
	plain.radiance.enabled = false;
	std::vector<Image> traced{}, analytic{};
	for (int frame{}; frame < 6; ++frame)
	{
		const glm::vec2 at{ static_cast<float>(frame) * 0.37f, static_cast<float>(frame) * 0.21f };
		ax::LightView view{ centred_view() };
		view.zoom = 2.0f;
		view.cameraPosition = at;
		const std::array lights{
			ax::Light2D{ .position = at, .radius = 2.6f * 64.0f, .intensity = 0.3f, .falloff = 1.5f, .sourceRadius = 19.2f },
			ax::Light2D{ .position = at, .radius = 9.0f * 64.0f, .intensity = 0.35f, .falloff = 1.2f,
				.direction = { 0.8f, 0.6f }, .coneInner = std::cos(0.30f), .coneOuter = std::cos(0.55f),
				.sourceRadius = 19.2f } };
		traced.push_back(render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, lights, {}, 1, view, {}, {}, nullptr, {}, {}, nullptr,
			{}, 2));
		analytic.push_back(render({ 1.0f, 1.0f, 1.0f, 1.0f }, plain, lights, {}, 1, view));
	}
	double excess{};
	int counted{};
	for (std::size_t frame{ 1 }; frame < traced.size(); ++frame)
		for (std::uint32_t y{}; y < s_size; ++y)
			for (std::uint32_t x{}; x < s_size; ++x, ++counted)
				excess += std::max(0, std::abs(traced[frame].at(x, y)[0] - traced[frame - 1].at(x, y)[0])
					- std::abs(analytic[frame].at(x, y)[0] - analytic[frame - 1].at(x, y)[0]));
	EXPECT_LT(excess / counted, 1.6);
}

TEST_F(LightRendererTest, DISABLED_RadianceMovingLightFlickerDump)
{
	// The player's lamp and torch (in world pixels, 64 per tile) seen zoomed out, moved a fraction of a GI texel each
	// frame: how much more each pixel changes from frame to frame than the analytic light does.
	ax::LightView view{ centred_view() };
	view.zoom = 0.25f;
	for (const auto follow : { false, true })
	for (const auto spacing : { 1u, 2u })
	{
		auto lighting{ radiance_settings() };
		lighting.radiance.probeSpacing = spacing;
		auto plain{ lighting };
		plain.radiance.enabled = false;
		std::vector<Image> traced{}, analytic{};
		for (int frame{}; frame < 16; ++frame)
		{
			const glm::vec2 at{ -100.0f + static_cast<float>(frame) * 0.7f, 10.0f + static_cast<float>(frame) * 0.3f }; view.cameraPosition = follow ? at + glm::vec2{ 100.0f, -10.0f } : glm::vec2{ 0.0f };
			const std::array lights{
				ax::Light2D{ .position = at, .radius = 2.6f * 64.0f, .intensity = 0.75f, .falloff = 1.5f,
					.sourceRadius = 0.3f * 64.0f },
				ax::Light2D{ .position = at, .radius = 9.0f * 64.0f, .intensity = 0.85f, .falloff = 1.2f,
					.direction = { 1.0f, 0.0f }, .coneInner = std::cos(0.30f), .coneOuter = std::cos(0.55f),
					.sourceRadius = 0.3f * 64.0f } };
			traced.push_back(render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, lights, {}, 1, view));
			analytic.push_back(render({ 1.0f, 1.0f, 1.0f, 1.0f }, plain, lights, {}, 1, view));
		}
		double excess{}, worst{};
		int counted{}, big{};
		double nearExcess{}, farExcess{};
		for (std::size_t frame{ 1 }; frame < traced.size(); ++frame)
			for (std::uint32_t y{}; y < s_size; ++y)
				for (std::uint32_t x{}; x < s_size; ++x)
				{
					const auto change{ std::abs(traced[frame].at(x, y)[0] - traced[frame - 1].at(x, y)[0]) };
					const auto expected{ std::abs(analytic[frame].at(x, y)[0] - analytic[frame - 1].at(x, y)[0]) };
					const auto extra{ std::max(0, change - expected) };
					excess += extra;
					{ const glm::vec2 lightAt{ follow ? glm::vec2{ 39.0f, 66.5f } : glm::vec2{ 39.0f + 0.175f * static_cast<float>(frame), 66.5f + 0.075f * static_cast<float>(frame) } };
					  (glm::length(glm::vec2{ static_cast<float>(x), static_cast<float>(y) } - lightAt) < 41.0f ? nearExcess : farExcess) += extra; }
					if (false) std::printf("  frame %zu (%u, %u): %d vs analytic %d\n", frame, x, y, change, expected);
					worst = std::max(worst, static_cast<double>(extra));
					big += extra > 6 ? 1 : 0;
					++counted;
				}
		std::printf("follow %d spacing %u: mean excess change %.3f, worst %.0f, over 6: %d\n", follow ? 1 : 0, spacing, excess / counted, worst, big);
		const auto& last{ traced.back() };
		std::vector<std::uint8_t> strip(s_size * 2 * s_size * 4);
		for (std::uint32_t y{}; y < s_size; ++y)
			for (std::uint32_t x{}; x < s_size * 2; ++x)
				for (std::uint32_t c{}; c < 4; ++c)
					strip[(y * s_size * 2 + x) * 4 + c] = x < s_size ? last.rgba[(y * s_size + x) * 4 + c]
						: static_cast<std::uint8_t>(std::min(255, 8 * std::abs(traced.back().at(x - s_size, y)[0]
							- traced[traced.size() - 2].at(x - s_size, y)[0])));
		for (std::uint32_t y{}; y < s_size; ++y)
			for (std::uint32_t x{ s_size }; x < s_size * 2; ++x) strip[(y * s_size * 2 + x) * 4 + 3] = 255;
		ax::write_png("radiance_flicker_" + std::to_string(spacing) + ".png", s_size * 2, s_size, 4, strip.data(),
			s_size * 2 * 4);
	}
}

TEST_F(LightRendererTest, DISABLED_RadianceTwoTreesDump)
{
	// Two trees side by side lit from above, as the game draws them at Medium, magnified.
	ax::LightView view{ centred_view() };
	view.zoom = 2.0f;
	const std::array lights{ ax::Light2D{ .position = { 6.0f, -200.0f }, .radius = 9.0f * 64.0f, .intensity = 0.85f,
		.falloff = 1.2f, .direction = { 0.96f, 0.28f }, .coneInner = std::cos(0.30f), .coneOuter = std::cos(0.55f),
		.sourceRadius = 0.3f * 64.0f } };
	const std::array circles{ ax::RadianceCircle{ .centre = { -16.0f, 0.0f }, .radius = 13.0f },
		ax::RadianceCircle{ .centre = { 15.0f, 2.0f }, .radius = 12.0f } };
	auto lighting{ radiance_settings() };
	lighting.radiance.divisor = 2;
	lighting.radiance.probeSpacing = 2;
	lighting.radiance.margin = 400.0f;
	lighting.radiance.maxMarginFraction = 10.0f;
	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, lights, {}, 1, view, {}, {}, nullptr, {}, circles) };
	constexpr std::uint32_t scale{ 4 };
	std::vector<std::uint8_t> big(s_size * scale * s_size * scale * 4);
	for (std::uint32_t y{}; y < s_size * scale; ++y)
		for (std::uint32_t x{}; x < s_size * scale; ++x)
			for (std::uint32_t c{}; c < 4; ++c)
				big[(y * s_size * scale + x) * 4 + c] = image.rgba[((y / scale) * s_size + x / scale) * 4 + c];
	ax::write_png("radiance_two_trees.png", s_size * scale, s_size * scale, 4, big.data(), s_size * scale * 4);
}

TEST_F(LightRendererTest, DISABLED_RadianceGameFlickerDump)
{
	// As in the game at Medium, zoomed in: the player's lamp and torch near two trees, the camera following the player
	// as it walks a fraction of a pixel each frame. Prints how much more pixels change than the analytic light does.
	char* zoomText{};
	std::size_t zoomLength{};
	_dupenv_s(&zoomText, &zoomLength, "AXENG_FLICKER_ZOOM");
	const auto zoom{ zoomText ? std::stof(zoomText) : 1.5f };
	std::free(zoomText);
	char* divisorText{};
	std::size_t divisorLength{};
	_dupenv_s(&divisorText, &divisorLength, "AXENG_FLICKER_DIVISOR");
	const auto divisor{ divisorText ? static_cast<std::uint32_t>(std::stoi(divisorText)) : 2u };
	std::free(divisorText);
	const std::array circles{ ax::RadianceCircle{ .centre = { 40.0f, -30.0f }, .radius = 13.0f },
		ax::RadianceCircle{ .centre = { -30.0f, 35.0f }, .radius = 11.0f } };
	for (const auto withTrees : { false, true })
	{
		auto lighting{ radiance_settings() };
		lighting.resolutionDivisor = 2;
		lighting.radiance = { .enabled = true, .divisor = static_cast<float>(divisor), .probeSpacing = 2, .margin = 640.0f, .maxMarginFraction = 0.3f,
			.aoStrength = 0.4f, .aoRange = 80.0f, .bounce = 0.25f };
		auto plain{ lighting };
		plain.radiance.enabled = false;
		std::vector<Image> traced{}, analytic{};
		for (int frame{}; frame < 12; ++frame)
		{
			const glm::vec2 at{ static_cast<float>(frame) * 6.1f, static_cast<float>(frame) * 2.3f };
			ax::LightView view{ centred_view() };
			view.zoom = zoom;
			// The player near the left edge, its torch across the view.
			view.cameraPosition = at + glm::vec2{ 0.8f * 64.0f / zoom, 0.0f };
			const std::array lights{
				ax::Light2D{ .position = at, .radius = 2.6f * 64.0f, .intensity = 0.75f, .falloff = 1.5f,
					.sourceRadius = 0.3f * 64.0f },
				ax::Light2D{ .position = at, .radius = 9.0f * 64.0f, .intensity = 0.85f, .falloff = 1.2f,
					.direction = { 0.96f, 0.28f }, .coneInner = std::cos(0.30f), .coneOuter = std::cos(0.55f),
					.sourceRadius = 0.3f * 64.0f } };
			const std::span<const ax::RadianceCircle> used{ withTrees ? std::span<const ax::RadianceCircle>{ circles }
				: std::span<const ax::RadianceCircle>{} };
			traced.push_back(render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, lights, {}, 1, view, {}, {}, nullptr, {}, used,
				nullptr, {}, 2));
			analytic.push_back(render({ 1.0f, 1.0f, 1.0f, 1.0f }, plain, lights, {}, 1, view));
		}
		double excess{}, worst{};
		std::array<double, 10> bins{};
		int counted{}, big{};
		for (std::size_t frame{ 1 }; frame < traced.size(); ++frame)
			for (std::uint32_t y{}; y < s_size; ++y)
				for (std::uint32_t x{}; x < s_size; ++x, ++counted)
				{
					const auto extra{ std::max(0, std::abs(traced[frame].at(x, y)[0] - traced[frame - 1].at(x, y)[0])
						- std::abs(analytic[frame].at(x, y)[0] - analytic[frame - 1].at(x, y)[0])) };
					excess += extra;
					worst = std::max(worst, static_cast<double>(extra));
					big += extra > 4 ? 1 : 0;
					bins[std::min<std::size_t>(9, static_cast<std::size_t>(std::hypot(static_cast<float>(x) - 64.0f, static_cast<float>(y) - 64.0f) / 10.0f))] += extra;
				}
		std::printf("trees %d: mean excess %.3f, worst %.0f, over 4: %d\n", withTrees ? 1 : 0, excess / counted, worst, big);
		// How much each pixel's error against the analytic light varies as the light walks: crawling blotches.
		double crawl{};
		for (std::uint32_t y{}; y < s_size; ++y)
			for (std::uint32_t x{}; x < s_size; ++x)
			{
				double sum{}, square{};
				for (std::size_t frame{}; frame < traced.size(); ++frame)
				{
					const double error{ static_cast<double>(traced[frame].at(x, y)[0] - analytic[frame].at(x, y)[0]) };
					sum += error;
					square += error * error;
				}
				const auto n{ static_cast<double>(traced.size()) };
				crawl += std::sqrt(std::max(0.0, square / n - (sum / n) * (sum / n)));
			}
		std::printf(" crawl %.3f", crawl / (s_size * s_size));
		for (const auto bin : bins) std::printf(" %.0f", bin);
		std::printf("\n");
		// The traced light, then its error against the analytic light (grey is none) in three successive frames.
		std::vector<std::uint8_t> strip(s_size * 4 * s_size * 4);
		for (std::uint32_t y{}; y < s_size; ++y)
			for (std::uint32_t x{}; x < s_size * 4; ++x)
			{
				const auto panel{ x / s_size }, px{ x % s_size };
				const auto frame{ traced.size() - 4 + panel };
				const auto value{ panel == 0 ? traced.back().at(px, y)[0] : std::clamp(128 + 3 * (traced[frame].at(px, y)[0]
					- analytic[frame].at(px, y)[0]), 0, 255) };
				for (std::uint32_t c{}; c < 3; ++c) strip[(y * s_size * 4 + x) * 4 + c] = static_cast<std::uint8_t>(value);
				strip[(y * s_size * 4 + x) * 4 + 3] = 255;
			}
		ax::write_png(std::string{ "radiance_game_flicker_" } + (withTrees ? "trees" : "open") + ".png", s_size * 4, s_size,
			4, strip.data(), s_size * 4 * 4);
	}
}

TEST_F(LightRendererTest, DISABLED_RadianceNearProfileDump)
{
	// Traced against analytic along a line from a light, at the GI divisor of Medium: the near part meeting the cascades.
	for (const auto source : { 0.0f, 19.2f })
		for (const auto spacing : { 1u, 2u })
		{
			const std::array lights{ ax::Light2D{ .position = { -60.0f, 0.0f }, .radius = 120.0f, .intensity = 0.5f,
				.falloff = 1.5f, .sourceRadius = source } };
			auto lighting{ radiance_settings() };
			lighting.radiance.divisor = 2;
			lighting.radiance.probeSpacing = spacing;
			auto plain{ lighting };
			plain.radiance.enabled = false;
			const auto traced{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, lights) };
			const auto analytic{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, plain, lights) };
			std::printf("source %.0f spacing %u:", source, spacing);
			for (std::uint32_t x{ 4 }; x < s_size; x += 6)
				std::printf(" %d/%d", traced.at(x, 64)[0], analytic.at(x, 64)[0]);
			std::printf("%c", 10);
		}
}

TEST_F(LightRendererTest, DISABLED_RadianceOmniVariantsDump)
{
	// The player's lamp alone, traced against analytic halfway out, varying one setting at a time.
	struct Variant { const char* name; float zoom; std::uint32_t mapDivisor; int frames; float source; glm::vec2 camera; };
	for (const auto& variant : { Variant{ "base", 0.375f, 2, 2, 19.2f, { 136.0f, 0.0f } },
		Variant{ "map1", 0.375f, 1, 2, 19.2f, { 136.0f, 0.0f } }, Variant{ "frames1", 0.375f, 2, 1, 19.2f, { 136.0f, 0.0f } },
		Variant{ "source0", 0.375f, 2, 2, 0.0f, { 136.0f, 0.0f } }, Variant{ "centred", 0.375f, 2, 2, 19.2f, { 0.0f, 0.0f } },
		Variant{ "zoom1", 1.0f, 2, 2, 19.2f, { 0.0f, 0.0f } }, Variant{ "nobounce", 0.375f, 2, 2, 19.2f, { 136.0f, 0.0f } } })
	{
		auto lighting{ radiance_settings() };
		lighting.resolutionDivisor = variant.mapDivisor;
		lighting.radiance = { .enabled = true, .divisor = 1, .probeSpacing = 2, .margin = 640.0f, .maxMarginFraction = 0.3f };
		if (std::string{ variant.name } == "nobounce") lighting.radiance.bounce = 0.0f;
		auto plain{ lighting };
		plain.radiance.enabled = false;
		ax::LightView view{ centred_view() };
		view.zoom = variant.zoom;
		view.cameraPosition = variant.camera;
		const std::array lights{ ax::Light2D{ .position = { 0.0f, 0.0f }, .radius = 2.6f * 64.0f, .intensity = 0.75f,
			.falloff = 1.5f, .sourceRadius = variant.source } };
		ax::LightRenderer::Stats stats{};
		const auto traced{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, lights, {}, 1, view, {}, {}, nullptr, {}, {}, &stats,
			{}, variant.frames) };
		const auto analytic{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, plain, lights, {}, 1, view) };
		const auto centre{ glm::vec2{ 64.0f } - variant.camera * variant.zoom };
		std::printf("%s (traced %u, cascades %u):", variant.name, stats.radianceLights, stats.radianceCascades);
		for (const auto fraction : { 0.2f, 0.4f, 0.6f, 0.8f })
		{
			const auto x{ static_cast<std::uint32_t>(std::clamp(centre.x + fraction * 166.4f * variant.zoom, 0.0f, 127.0f)) };
			std::printf(" %d/%d", traced.at(x, 64)[0], analytic.at(x, 64)[0]);
		}
		std::printf("%c", 10);
	}
}

TEST_F(LightRendererTest, RadianceLampHandsOverToTheCascadesWithoutARing)
{
	// A game lamp (10 tiles, source half a tile, 32 pixel tiles) at Medium zoomed in and out: out from it, where its near
	// part hands over to the cascades, the light stays close to the plain light, with no bright or dark ring.
	for (const auto zoom : { 1.05f, 0.43f })
	{
		auto lighting{ radiance_settings() };
		lighting.resolutionDivisor = 2;
		lighting.ambient = glm::vec3{ 0.15f };
		lighting.radiance = { .enabled = true, .divisor = 1, .probeSpacing = 2, .margin = 640.0f, .maxMarginFraction = 0.3f };
		auto plain{ lighting };
		plain.radiance.enabled = false;
		ax::LightView view{ centred_view() };
		view.zoom = zoom;
		view.cameraPosition = { 60.0f / zoom, 0.0f };
		const std::array lights{ ax::Light2D{ .radius = 320.0f, .intensity = 1.0f, .colour = { 0.8f, 0.4f, 1.0f },
			.falloff = 1.6f, .sourceRadius = 16.0f } };
		const auto traced{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, lights, {}, 1, view) };
		const auto analytic{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, plain, lights, {}, 1, view) };
		int worst{};
		for (float tiles{ 0.6f }; tiles < 3.0f; tiles += 0.05f)
		{
			const auto x{ static_cast<std::uint32_t>(4.0f + tiles * 32.0f * zoom) };
			if (x >= s_size) break;
			worst = std::max(worst, std::abs(traced.at(x, 64)[1] - analytic.at(x, 64)[1]));
		}
		// The old, short span overshot by 22.
		EXPECT_LE(worst, 14) << "zoom " << zoom;
	}
}

TEST_F(LightRendererTest, DISABLED_RadianceZoomSweepDump)
{
	// The game's view at Medium (1920x1080, 32 pixel tiles, lamps 10 tiles across) zoomed from 0.8 to 1.5: what the
	// cascades do at each zoom, and the light map's mean brightness, to find jumps.
	constexpr std::uint32_t width{ 1920 }, height{ 1080 };
	constexpr auto format{ wgpu::TextureFormat::RGBA8Unorm };
	wgpu::TextureDescriptor desc{};
	desc.size = { width, height };
	desc.format = format;
	desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
	const auto target{ s_device.CreateTexture(&desc) };
	const auto view{ target.CreateView() };
	ax::LightRenderer renderer{};
	ASSERT_TRUE(renderer.init(s_device, format, 1));
	std::vector<ax::Light2D> lights{};
	for (std::uint32_t index{}; index < 60; ++index)
		lights.push_back({ .position = { static_cast<float>((index * 7919u) % 3200u) - 1600.0f,
			static_cast<float>((index * 104729u) % 1800u) - 900.0f }, .radius = 320.0f, .intensity = 1.0f,
			.colour = { 1.0f, 0.9f, 0.7f }, .falloff = 1.6f, .sourceRadius = 16.0f });
	std::vector<ax::RadianceCircle> circles{};
	for (std::uint32_t index{}; index < 400; ++index)
		circles.push_back({ .centre = { static_cast<float>((index * 3571u) % 3800u) - 1900.0f,
			static_cast<float>((index * 6151u) % 2200u) - 1100.0f }, .radius = 13.0f });
	constexpr std::uint32_t bytesPerRow{ width * 4 };
	wgpu::BufferDescriptor readbackDesc{};
	readbackDesc.size = bytesPerRow * height;
	readbackDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
	const auto readback{ s_device.CreateBuffer(&readbackDesc) };
	// World points inside the view at every zoom, sampled at each step.
	std::vector<glm::vec2> points{};
	for (int y{ -15 }; y <= 15; ++y)
		for (int x{ -28 }; x <= 28; ++x)
			points.push_back({ static_cast<float>(x) * 20.0f, static_cast<float>(y) * 20.0f });
	std::array<std::vector<std::vector<int>>, 2> samples{};
	for (const auto enabled : { false, true })
	for (float zoom{ 0.80f }; zoom <= 1.501f; zoom += 0.01f)
	{
		auto settings{ ax::LightingSettings{ .enabled = true, .ambient = glm::vec3{ 0.12f }, .resolutionDivisor = 2 } };
		settings.radiance = { .enabled = enabled, .divisor = 2, .probeSpacing = 2, .margin = 320.0f, .maxMarginFraction = 0.3f,
			.aoStrength = 0.4f, .aoRange = 40.0f, .bounce = 0.25f };
		const ax::LightView camera{ .viewport = { static_cast<float>(width), static_cast<float>(height) }, .zoom = zoom };
		double mean{};
		for (int frame{}; frame < 2; ++frame)
		{
			auto encoder{ s_device.CreateCommandEncoder() };
			{
				wgpu::RenderPassColorAttachment colour{};
				colour.view = view;
				colour.loadOp = wgpu::LoadOp::Clear;
				colour.storeOp = wgpu::StoreOp::Store;
				colour.clearValue = { 0.5, 0.5, 0.5, 1.0 };
				wgpu::RenderPassDescriptor passDesc{};
				passDesc.colorAttachmentCount = 1;
				passDesc.colorAttachments = &colour;
				encoder.BeginRenderPass(&passDesc).End();
			}
			renderer.encode(s_device, s_device.GetQueue(), encoder, view, width, height, camera, settings, lights, {}, {},
				nullptr, {}, circles);
			wgpu::TexelCopyTextureInfo source{};
			source.texture = target;
			wgpu::TexelCopyBufferInfo destination{};
			destination.buffer = readback;
			destination.layout.bytesPerRow = bytesPerRow;
			destination.layout.rowsPerImage = height;
			const wgpu::Extent3D extent{ width, height };
			encoder.CopyTextureToBuffer(&source, &destination, &extent);
			const auto commands{ encoder.Finish() };
			s_device.GetQueue().Submit(1, &commands);
			bool mapped{};
			s_instance.WaitAny(readback.MapAsync(wgpu::MapMode::Read, 0, readbackDesc.size, wgpu::CallbackMode::WaitAnyOnly,
				[&mapped](wgpu::MapAsyncStatus status, wgpu::StringView) { mapped = status == wgpu::MapAsyncStatus::Success; }),
				UINT64_MAX);
			if (mapped)
			{
				const auto* pixels{ static_cast<const std::uint8_t*>(readback.GetConstMappedRange()) };
				std::uint64_t sum{};
				for (std::size_t index{}; index < readbackDesc.size; index += 4) sum += pixels[index + 1];
				mean = static_cast<double>(sum) / (width * height);
				if (frame == 1)
				{
					auto& row{ samples[enabled ? 1 : 0].emplace_back() };
					for (const auto& point : points)
					{
						const auto sx{ static_cast<std::uint32_t>(point.x * zoom + width * 0.5f) };
						const auto sy{ static_cast<std::uint32_t>(point.y * zoom + height * 0.5f) };
						row.push_back(pixels[(sy * width + sx) * 4 + 1]);
					}
				}
				readback.Unmap();
			}
		}
		const auto stats{ renderer.last_stats() };
		if (enabled) std::printf("zoom %.2f: mean %.2f traced %u cascades %u grid %ux%u\n", zoom, mean, stats.radianceLights,
			stats.radianceCascades, stats.radianceWidth, stats.radianceHeight);
	}
	double excess{};
	for (std::size_t step{ 1 }; step < samples[1].size(); ++step)
	{
		double stepExcess{};
		for (std::size_t point{}; point < points.size(); ++point)
			stepExcess += std::max(0, std::abs(samples[1][step][point] - samples[1][step - 1][point])
				- std::abs(samples[0][step][point] - samples[0][step - 1][point]));
		std::printf(" %.2f:%.2f", 0.80 + 0.01 * static_cast<double>(step), stepExcess / points.size());
		excess += stepExcess;
	}
	std::printf("%c", 10);
	std::printf("zoom flicker: %.3f per point per step\n", excess / ((samples[1].size() - 1) * points.size()));
}

TEST_F(LightRendererTest, DISABLED_RadianceLampProfileDump)
{
	// A game lamp (10 tiles, source half a tile, 64 pixel tiles) at Medium's texel density for two zooms: traced against
	// analytic along a line out from it, with and without a lamp body (a circle) round it. Prints in tiles.
	struct Preset { const char* name; std::uint32_t spacing, fix; float divisor{ 2.0f }; };
	for (const auto preset : { Preset{ "medium", 2, 8 }, Preset{ "high", 2, 8, 1.4142f }, Preset{ "ultra fix 4", 1, 4 }, Preset{ "ultra fix 5", 1, 5 }, Preset{ "ultra fix all", 1, 8 } })
	for (const auto zoom : { 1.05f, 0.72f, 0.43f })
		for (const auto body : { true })
		{
			auto lighting{ radiance_settings() };
			lighting.resolutionDivisor = 2;
			lighting.ambient = glm::vec3{ 0.15f };
			lighting.radiance = { .enabled = true, .divisor = 1, .probeSpacing = 2, .margin = 640.0f, .maxMarginFraction = 0.3f,
				.aoStrength = 0.4f, .aoRange = 80.0f, .bounce = 0.25f, .fixCascades = preset.fix };
			lighting.radiance.probeSpacing = preset.spacing;
			lighting.radiance.divisor = preset.divisor;
			auto plain{ lighting };
			plain.radiance.enabled = false;
			ax::LightView view{ centred_view() };
			view.zoom = zoom;
			view.cameraPosition = { 60.0f / zoom, 0.0f };
			const std::array lights{ ax::Light2D{ .position = { 0.0f, 0.0f }, .radius = 320.0f, .intensity = 1.0f,
				.colour = { 0.8f, 0.4f, 1.0f }, .falloff = 1.6f, .sourceRadius = 16.0f } };
			// A lamp's head: a rounded square 0.8 tiles across, its corners 0.16 round.
			std::vector<ax::RadianceCircle> circles{ { .centre = { 0.0f, 0.0f }, .radius = 12.8f, .albedo = { 0.5f, 0.3f, 0.7f, 1.0f } } };
			for (const auto corner : { glm::vec2{ 1.0f, 1.0f }, glm::vec2{ -1.0f, 1.0f }, glm::vec2{ 1.0f, -1.0f }, glm::vec2{ -1.0f, -1.0f } })
				circles.push_back({ .centre = corner * 7.7f, .radius = 5.1f, .albedo = { 0.5f, 0.3f, 0.7f, 1.0f } });
			const auto traced{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, lights, {}, 1, view, {}, {}, nullptr, {},
				body ? std::span<const ax::RadianceCircle>{ circles } : std::span<const ax::RadianceCircle>{}, nullptr, {}, 3) };
			const auto analytic{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, plain, lights, {}, 1, view) };
			std::printf("%s zoom %.3f body %d:", preset.name, zoom, body ? 1 : 0);
			const auto lampX{ 64.0f - 60.0f };
			for (float tiles{ 0.25f }; tiles < 10.0f; tiles += 0.125f)
			{
				const auto x{ static_cast<std::uint32_t>(lampX + tiles * 32.0f * zoom) };
				if (x >= s_size) break;
				std::printf(" %.2f:%d/%d", tiles, traced.at(x, 64)[1], analytic.at(x, 64)[1]);
			}
			std::printf("%c", 10);
		}
}

TEST_F(LightRendererTest, DISABLED_RadianceInteriorDump)
{
	const std::array lights{ ax::Light2D{ .position = { -50.0f, -20.0f }, .radius = 120.0f, .intensity = 1.0f,
		.falloff = 1.0f } };
	const std::array circles{ ax::RadianceCircle{ .centre = { 0.0f, 0.0f }, .radius = 22.0f },
		ax::RadianceCircle{ .centre = { 30.0f, 40.0f }, .radius = 9.0f } };
	for (const auto spacing : { 1u, 2u })
	{
		auto lighting{ radiance_settings() };
		lighting.radiance.divisor = 2;
		lighting.radiance.probeSpacing = spacing;
		lighting.radiance.bilinearFix = true;
		const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, lights, {}, 1, centred_view(), {}, {}, nullptr, {},
			circles) };
		constexpr std::uint32_t scale{ 4 };
		std::vector<std::uint8_t> big(s_size * scale * s_size * scale * 4);
		for (std::uint32_t y{}; y < s_size * scale; ++y)
			for (std::uint32_t x{}; x < s_size * scale; ++x)
				for (std::uint32_t c{}; c < 4; ++c)
					big[(y * s_size * scale + x) * 4 + c] = image.rgba[((y / scale) * s_size + x / scale) * 4 + c];
		ax::write_png("radiance_interior_" + std::to_string(spacing) + ".png", s_size * scale, s_size * scale, 4,
			big.data(), s_size * scale * 4);
	}
}

TEST_F(LightRendererTest, RadianceMarchedLightIsExactInTheOpenAndShadowedBehindOccluders)
{
	// A light with marched shadows is drawn plainly, so in the open it is exactly the analytic light; behind a circle it
	// is dark, with a soft edge; it is not traced.
	const std::array lights{ ax::Light2D{ .position = { -36.0f, 0.0f }, .radius = 80.0f, .intensity = 1.0f,
		.falloff = 1.0f, .sourceRadius = 4.0f, .shadows = ax::Light2D::marched_shadows } };
	const std::array circles{ ax::RadianceCircle{ .centre = { -12.0f, 0.0f }, .radius = 6.0f } };
	auto plain{ radiance_settings() };
	plain.radiance.enabled = false;
	const auto analytic{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, plain, lights) };
	ax::LightRenderer::Stats stats{};
	const auto open{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, radiance_settings(), lights, {}, 1, centred_view(), {}, {}, nullptr,
		{}, {}, &stats) };
	EXPECT_EQ(stats.radianceLights, 0u);
	for (const auto x : { -50.0f, -20.0f, 0.0f, 20.0f, 30.0f })
		EXPECT_TRUE(close_to(open.at(at(x), at(5.0f)), analytic.at(at(x), at(5.0f)), 1)) << x;
	const auto shadowed{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, radiance_settings(), lights, {}, 1, centred_view(), {}, {},
		nullptr, {}, circles) };
	EXPECT_LT(shadowed.at(at(20.0f), at(0.0f))[0], analytic.at(at(20.0f), at(0.0f))[0] / 4);
	EXPECT_TRUE(close_to(shadowed.at(at(-36.0f), at(30.0f)), analytic.at(at(-36.0f), at(30.0f)), 2));
	int partial{};
	for (float y{}; y < 30.0f; y += 1.0f)
	{
		const auto ratio{ static_cast<float>(shadowed.at(at(20.0f), at(y))[0])
			/ static_cast<float>(std::max(analytic.at(at(20.0f), at(y))[0], 1)) };
		partial += ratio > 0.15f && ratio < 0.85f ? 1 : 0;
	}
	EXPECT_GE(partial, 3);
	// The circle's top is lit on the light's side and dark on the far side.
	EXPECT_GT(shadowed.at(at(-17.0f), at(0.0f))[0], shadowed.at(at(-7.0f), at(0.0f))[0] + 30);
}

TEST_F(LightRendererTest, RadianceMarchedLightDoesNotCrawlAsItMoves)
{
	// The player's torch walking past a tree with marched shadows: its error against the plain light (the tree's shadow)
	// moves with it rather than crawling over a fixed grid.
	const std::array circles{ ax::RadianceCircle{ .centre = { 170.0f, 40.0f }, .radius = 12.0f } };
	auto lighting{ radiance_settings() };
	lighting.resolutionDivisor = 2;
	lighting.radiance = { .enabled = true, .divisor = 1, .probeSpacing = 2, .margin = 640.0f, .maxMarginFraction = 0.3f };
	auto plain{ lighting };
	plain.radiance.enabled = false;
	double crawl{};
	std::vector<Image> traced{}, analytic{};
	for (int frame{}; frame < 6; ++frame)
	{
		const glm::vec2 at{ static_cast<float>(frame) * 6.1f, static_cast<float>(frame) * 2.3f };
		ax::LightView view{ centred_view() };
		view.zoom = 0.375f;
		view.cameraPosition = at + glm::vec2{ 136.0f, 0.0f };
		const std::array lights{ ax::Light2D{ .position = at, .radius = 9.0f * 64.0f, .intensity = 0.85f, .falloff = 1.2f,
			.direction = { 0.96f, 0.28f }, .coneInner = std::cos(0.30f), .coneOuter = std::cos(0.55f), .sourceRadius = 19.2f,
			.shadows = ax::Light2D::marched_shadows } };
		traced.push_back(render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, lights, {}, 1, view, {}, {}, nullptr, {}, circles));
		analytic.push_back(render({ 1.0f, 1.0f, 1.0f, 1.0f }, plain, lights, {}, 1, view));
	}
	// Away from the tree's shadow (the left two thirds, before it), traced and plain agree in every frame.
	for (std::size_t frame{}; frame < traced.size(); ++frame)
		for (std::uint32_t y{}; y < s_size; ++y)
			for (std::uint32_t x{}; x < 40; ++x)
				crawl += std::abs(traced[frame].at(x, y)[0] - analytic[frame].at(x, y)[0]);
	EXPECT_LT(crawl / (traced.size() * s_size * 40), 0.5);
}

TEST_F(LightRendererTest, RadianceLampOutsideTheGridStillCastsShadows)
{
	// A lamp beyond the grid's margin cannot be traced, but its light reaching into the view is still shadowed (marched):
	// otherwise lamps would lose their shadows at once as they cross the grid's edge while the view moves or zooms.
	auto lighting{ radiance_settings() };
	lighting.radiance.margin = 8.0f;
	lighting.radiance.maxMarginFraction = 0.1f;
	const std::array lights{ ax::Light2D{ .position = { -110.0f, 0.0f }, .radius = 200.0f, .intensity = 1.0f,
		.falloff = 1.0f, .sourceRadius = 4.0f } };
	const std::array circles{ ax::RadianceCircle{ .centre = { -20.0f, 0.0f }, .radius = 8.0f } };
	ax::LightRenderer::Stats stats{};
	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, lights, {}, 1, centred_view(), {}, {}, nullptr, {},
		circles, &stats) };
	EXPECT_EQ(stats.radianceLights, 0u);
	auto plain{ lighting };
	plain.radiance.enabled = false;
	const auto analytic{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, plain, lights) };
	EXPECT_LT(image.at(at(20.0f), at(0.0f))[0], analytic.at(at(20.0f), at(0.0f))[0] / 3);
	EXPECT_TRUE(close_to(image.at(at(20.0f), at(40.0f)), analytic.at(at(20.0f), at(40.0f)), 3));
}

TEST_F(LightRendererTest, RadianceLampsDoNotBlotOutEachOthersLight)
{
	// A purple lamp in a white lamp's light (32 pixel tiles, game sizes): round the purple lamp the white light is still
	// there, not lost to probes inside the purple lamp's source, which see only it.
	auto lighting{ radiance_settings() };
	lighting.resolutionDivisor = 2;
	lighting.ambient = glm::vec3{ 0.1f };
	lighting.radiance = { .enabled = true, .divisor = 1, .probeSpacing = 2, .margin = 640.0f, .maxMarginFraction = 0.3f };
	auto plain{ lighting };
	plain.radiance.enabled = false;
	ax::LightView view{ centred_view() };
	view.zoom = 0.7f;
	const std::array lights{
		ax::Light2D{ .position = { -120.0f, 0.0f }, .radius = 320.0f, .intensity = 0.6f, .colour = { 1.0f, 1.0f, 1.0f },
			.falloff = 1.6f, .sourceRadius = 16.0f },
		ax::Light2D{ .position = { 0.0f, 0.0f }, .radius = 320.0f, .intensity = 0.6f, .colour = { 0.75f, 0.0f, 1.0f },
			.falloff = 1.6f, .sourceRadius = 16.0f } };
	const auto traced{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, lights, {}, 1, view) };
	const auto analytic{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, plain, lights, {}, 1, view) };
	// Green comes only from the white lamp.
	int worst{};
	for (int dy{ -40 }; dy <= 40; dy += 2)
		for (int dx{ -40 }; dx <= 40; dx += 2)
		{
			const auto r{ std::hypot(static_cast<float>(dx), static_cast<float>(dy)) / 0.7f };
			if (r < 18.0f || r > 56.0f) continue;
			const auto x{ static_cast<std::uint32_t>(64 + dx) }, y{ static_cast<std::uint32_t>(64 + dy) };
			worst = std::max(worst, analytic.at(x, y)[1] - traced.at(x, y)[1]);
		}
	EXPECT_LE(worst, 12);
}

TEST_F(LightRendererTest, RadianceFadesBetweenGridDensitiesWhenZooming)
{
	// Zooming across a half octave (1.21 to 1.10 at Medium) changes the grid's density, which shifts the traced light a
	// little; it fades over several frames rather than jumping in one.
	constexpr std::uint32_t size{ 256 };
	constexpr auto format{ wgpu::TextureFormat::RGBA8Unorm };
	wgpu::TextureDescriptor desc{};
	desc.size = { size, size };
	desc.format = format;
	desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
	const auto target{ s_device.CreateTexture(&desc) };
	const auto targetView{ target.CreateView() };
	ax::LightRenderer renderer{};
	ASSERT_TRUE(renderer.init(s_device, format, 1));
	std::vector<ax::Light2D> lights{};
	for (int index{}; index < 6; ++index)
		lights.push_back({ .position = { static_cast<float>(index % 3) * 70.0f - 70.0f, static_cast<float>(index / 3) * 80.0f - 40.0f },
			.radius = 320.0f, .intensity = 0.4f, .falloff = 1.6f, .sourceRadius = 16.0f });
	std::vector<ax::RadianceCircle> circles{};
	for (int index{}; index < 12; ++index)
		circles.push_back({ .centre = { static_cast<float>((index * 37) % 200) - 100.0f, static_cast<float>((index * 53) % 160) - 80.0f },
			.radius = 9.0f });
	auto settings{ ax::LightingSettings{ .enabled = true, .ambient = glm::vec3{ 0.1f }, .resolutionDivisor = 2 } };
	settings.radiance = { .enabled = true, .divisor = 2, .probeSpacing = 2, .margin = 320.0f, .maxMarginFraction = 0.3f };
	constexpr std::uint32_t bytesPerRow{ size * 4 };
	wgpu::BufferDescriptor readbackDesc{};
	readbackDesc.size = bytesPerRow * size;
	readbackDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
	const auto readback{ s_device.CreateBuffer(&readbackDesc) };
	// The light at world points every 8 pixels within 90 of the middle, frame by frame.
	const auto frame{ [&](float zoom)
	{
		auto encoder{ s_device.CreateCommandEncoder() };
		{
			wgpu::RenderPassColorAttachment colour{};
			colour.view = targetView;
			colour.loadOp = wgpu::LoadOp::Clear;
			colour.storeOp = wgpu::StoreOp::Store;
			colour.clearValue = { 1.0, 1.0, 1.0, 1.0 };
			wgpu::RenderPassDescriptor passDesc{};
			passDesc.colorAttachmentCount = 1;
			passDesc.colorAttachments = &colour;
			encoder.BeginRenderPass(&passDesc).End();
		}
		const ax::LightView view{ .viewport = { static_cast<float>(size), static_cast<float>(size) }, .zoom = zoom };
		renderer.encode(s_device, s_device.GetQueue(), encoder, targetView, size, size, view, settings, lights, {}, {},
			nullptr, {}, circles);
		wgpu::TexelCopyTextureInfo source{};
		source.texture = target;
		wgpu::TexelCopyBufferInfo destination{};
		destination.buffer = readback;
		destination.layout.bytesPerRow = bytesPerRow;
		destination.layout.rowsPerImage = size;
		const wgpu::Extent3D extent{ size, size };
		encoder.CopyTextureToBuffer(&source, &destination, &extent);
		const auto commands{ encoder.Finish() };
		s_device.GetQueue().Submit(1, &commands);
		bool mapped{};
		s_instance.WaitAny(readback.MapAsync(wgpu::MapMode::Read, 0, readbackDesc.size, wgpu::CallbackMode::WaitAnyOnly,
			[&mapped](wgpu::MapAsyncStatus status, wgpu::StringView) { mapped = status == wgpu::MapAsyncStatus::Success; }),
			UINT64_MAX);
		std::vector<int> values{};
		if (mapped)
		{
			const auto* pixels{ static_cast<const std::uint8_t*>(readback.GetConstMappedRange()) };
			for (float y{ -90.0f }; y <= 90.0f; y += 8.0f)
				for (float x{ -90.0f }; x <= 90.0f; x += 8.0f)
				{
					const auto sx{ static_cast<std::uint32_t>(x * zoom + size * 0.5f) };
					const auto sy{ static_cast<std::uint32_t>(y * zoom + size * 0.5f) };
					values.push_back(pixels[(sy * size + sx) * 4 + 1]);
				}
			readback.Unmap();
		}
		return values;
	} };
	const auto change{ [](const std::vector<int>& a, const std::vector<int>& b)
	{
		double sum{};
		for (std::size_t index{}; index < a.size(); ++index) sum += std::abs(a[index] - b[index]);
		return sum / static_cast<double>(a.size());
	} };
	for (int index{}; index < 3; ++index) frame(1.21f);
	auto before{ frame(1.21f) };
	// The light at 1.10 once settled, against which the whole shift is measured.
	std::vector<std::vector<int>> after{};
	for (int index{}; index < 20; ++index) after.push_back(frame(1.10f));
	// Zooming changes which pixels the points land on a little, so this compares against the settled frame too.
	const auto whole{ change(before, after.back()) };
	double worstStep{ change(before, after.front()) };
	for (std::size_t index{ 1 }; index < after.size(); ++index) worstStep = std::max(worstStep, change(after[index - 1], after[index]));
	EXPECT_GT(whole, 0.3);
	EXPECT_LT(worstStep, whole * 0.5) << "whole " << whole;
}

TEST_F(LightRendererTest, RadianceLampsBodyShadowsOtherLightButLetsItsOwnOut)
{
	// A lamp whose body (a circle) sits inside its source disc: the body is the lamp's, so it blocks another lamp's light
	// behind it, while its own light still leaves it; the rest of the disc lets light through.
	auto lighting{ radiance_settings() };
	lighting.radiance.probeSpacing = 2;
	const std::array lamp{ ax::Light2D{ .position = { 0.0f, 0.0f }, .radius = 60.0f, .intensity = 1.0f,
		.colour = { 1.0f, 0.0f, 0.0f }, .falloff = 1.0f,
		.sourceRadius = 10.0f } };
	const std::array both{ ax::Light2D{ .position = { -50.0f, 0.0f }, .radius = 160.0f, .intensity = 0.5f,
		.colour = { 0.0f, 1.0f, 0.0f }, .falloff = 1.0f, .sourceRadius = 2.0f }, lamp[0] };
	const std::array body{ ax::RadianceCircle{ .centre = { 0.0f, 0.0f }, .radius = 5.0f } };
	auto plain{ lighting };
	plain.radiance.enabled = false;
	// Its own light, red, matches the plain light round it.
	const auto own{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, lamp, {}, 1, centred_view(), {}, {}, nullptr, {}, body) };
	const auto ownPlain{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, plain, lamp) };
	EXPECT_TRUE(close_to(own.at(at(30.0f), at(0.0f)), ownPlain.at(at(30.0f), at(0.0f)), 16));
	EXPECT_TRUE(close_to(own.at(at(-30.0f), at(10.0f)), ownPlain.at(at(-30.0f), at(10.0f)), 16));
	// The other lamp's light (green, from the left) is shadowed straight behind the body, not beside it.
	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, both, {}, 1, centred_view(), {}, {}, nullptr, {}, body) };
	const auto behind{ image.at(at(20.0f), at(0.0f))[1] }, beside{ image.at(at(20.0f), at(14.0f))[1] };
	EXPECT_LT(behind, beside - 30) << behind << " " << beside;
}

TEST_F(LightRendererTest, RadianceLampsBodyCornersCastNoSeams)
{
	// A lamp's body is a rounded square whose corners reach just inside its source (a middle circle and four at the
	// corners here). Drawn on the grid, a corner texel must not poke out of the disc and cast a thin seam of shadow
	// across the lamp's own light along the diagonals.
	for (const auto zoom : { 1.0f, 1.3f, 0.8f })
	{
		auto lighting{ radiance_settings() };
		lighting.radiance.probeSpacing = 2;
		ax::LightView view{ centred_view() };
		view.zoom = zoom;
		const std::array lamp{ ax::Light2D{ .position = { 0.3f, 0.2f }, .radius = 60.0f, .intensity = 0.5f, .falloff = 1.0f,
			.sourceRadius = 10.0f } };
		std::vector<ax::RadianceCircle> body{ { .centre = { 0.3f, 0.2f }, .radius = 6.0f } };
		for (const auto corner : { glm::vec2{ 1.0f, 1.0f }, glm::vec2{ -1.0f, 1.0f }, glm::vec2{ 1.0f, -1.0f }, glm::vec2{ -1.0f, -1.0f } })
			body.push_back({ .centre = glm::vec2{ 0.3f, 0.2f } + corner * 5.0f, .radius = 2.9f });
		auto plain{ lighting };
		plain.radiance.enabled = false;
		const auto traced{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, lamp, {}, 1, view, {}, {}, nullptr, {}, body) };
		const auto analytic{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, plain, lamp, {}, 1, view) };
		// Along each diagonal, out past the body, no pixel much darker than the plain light.
		int worst{};
		for (const auto corner : { glm::vec2{ 1.0f, 1.0f }, glm::vec2{ -1.0f, 1.0f }, glm::vec2{ 1.0f, -1.0f }, glm::vec2{ -1.0f, -1.0f } })
			for (float r{ 16.0f }; r < 40.0f; r += 1.0f)
			{
				const auto world{ corner * 0.7071f * r };
				const auto x{ static_cast<std::uint32_t>(64.0f + world.x * zoom) };
				const auto y{ static_cast<std::uint32_t>(64.0f + world.y * zoom) };
				worst = std::max(worst, analytic.at(x, y)[0] - traced.at(x, y)[0]);
			}
		EXPECT_LE(worst, 16) << "zoom " << zoom;
	}
}

TEST_F(LightRendererTest, RadianceMarchedShadowOfALampIsSoft)
{
	// The player's light (marched) past a lamp: the lamp's body, inside its see-through disc, casts a shadow with a soft
	// edge like any other occluder's, not a hard one.
	auto lighting{ radiance_settings() };
	lighting.radiance.probeSpacing = 2;
	const std::array lights{
		ax::Light2D{ .position = { -40.0f, 0.0f }, .radius = 120.0f, .intensity = 1.0f, .colour = { 0.0f, 1.0f, 0.0f },
			.falloff = 1.0f, .sourceRadius = 4.0f, .shadows = ax::Light2D::marched_shadows },
		ax::Light2D{ .position = { -12.0f, 0.0f }, .radius = 40.0f, .intensity = 0.3f, .colour = { 1.0f, 0.0f, 0.0f },
			.falloff = 1.0f, .sourceRadius = 7.0f } };
	const std::array body{ ax::RadianceCircle{ .centre = { -12.0f, 0.0f }, .radius = 6.0f } };
	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, lights, {}, 1, centred_view(), {}, {}, nullptr, {}, body) };
	auto open{ lighting };
	open.radiance.enabled = false;
	const auto plain{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, open, lights) };
	// Green (the marched light) behind the lamp: dark in the middle, partly lit across the shadow's edge.
	EXPECT_LT(image.at(at(20.0f), at(0.0f))[1], plain.at(at(20.0f), at(0.0f))[1] / 3);
	int partial{};
	for (float y{}; y < 30.0f; y += 1.0f)
	{
		const auto ratio{ static_cast<float>(image.at(at(20.0f), at(y))[1])
			/ static_cast<float>(std::max(plain.at(at(20.0f), at(y))[1], 1)) };
		partial += ratio > 0.15f && ratio < 0.85f ? 1 : 0;
	}
	EXPECT_GE(partial, 3);
}

TEST_F(LightRendererTest, RadianceReusesUnchangedCascades)
{
	// Frames where nothing the cascades read changes reuse their last result; a lamp moving, an occluder appearing or
	// the grid moving traces them again.
	constexpr std::uint32_t size{ 128 };
	wgpu::TextureDescriptor desc{};
	desc.size = { size, size };
	desc.format = wgpu::TextureFormat::RGBA8Unorm;
	desc.usage = wgpu::TextureUsage::RenderAttachment;
	const auto target{ s_device.CreateTexture(&desc) };
	const auto targetView{ target.CreateView() };
	ax::LightRenderer renderer{};
	ASSERT_TRUE(renderer.init(s_device, desc.format, 1));
	std::array lights{ ax::Light2D{ .position = { -20.0f, 0.0f }, .radius = 80.0f, .sourceRadius = 4.0f } };
	std::vector<ax::RadianceCircle> circles{ { .centre = { 10.0f, 0.0f }, .radius = 6.0f } };
	ax::LightView view{ centred_view() };
	const auto frame{ [&]
	{
		auto encoder{ s_device.CreateCommandEncoder() };
		renderer.encode(s_device, s_device.GetQueue(), encoder, targetView, size, size, view, radiance_settings(), lights,
			{}, {}, nullptr, {}, circles);
		const auto commands{ encoder.Finish() };
		s_device.GetQueue().Submit(1, &commands);
		renderer.submitted();
		return renderer.last_stats().radianceReused;
	} };
	frame();
	frame();
	EXPECT_TRUE(frame());
	lights[0].position.x += 1.0f;
	EXPECT_FALSE(frame());
	EXPECT_TRUE(frame());
	circles.push_back({ .centre = { 10.0f, 20.0f }, .radius = 4.0f });
	EXPECT_FALSE(frame());
	EXPECT_TRUE(frame());
	view.cameraPosition.x += 200.0f;
	EXPECT_FALSE(frame());
}

TEST_F(LightRendererTest, RadianceOccluderCastsASoftShadow)
{
	const std::array lights{ ax::Light2D{ .position = { -36.0f, 0.0f }, .radius = 80.0f, .intensity = 1.0f,
		.falloff = 1.0f } };
	const std::array circles{ ax::RadianceCircle{ .centre = { -12.0f, 0.0f }, .radius = 6.0f } };
	const auto open{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, radiance_settings(), lights) };
	const auto shadowed{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, radiance_settings(), lights, {}, 1, centred_view(), {}, {},
		nullptr, {}, circles) };
	// Behind the circle: dark. Beside the shadow: as lit as without the circle.
	const auto behind{ shadowed.at(at(20.0f), at(0.0f))[0] };
	EXPECT_LT(behind, open.at(at(20.0f), at(0.0f))[0] / 3) << behind;
	EXPECT_TRUE(close_to(shadowed.at(at(-36.0f), at(30.0f)), open.at(at(-36.0f), at(30.0f)), 12));
	// The shadow's edge is soft: across it, behind the circle, several pixels are partly lit.
	int partial{};
	for (float y{}; y < 30.0f; y += 1.0f)
	{
		const auto ratio{ static_cast<float>(shadowed.at(at(20.0f), at(y))[0])
			/ static_cast<float>(std::max(open.at(at(20.0f), at(y))[0], 1)) };
		partial += ratio > 0.15f && ratio < 0.85f ? 1 : 0;
	}
	EXPECT_GE(partial, 3);
}

TEST_F(LightRendererTest, RadianceLightsAnOccludersInteriorFromItsLitSide)
{
	const std::array lights{ ax::Light2D{ .position = { -40.0f, 0.0f }, .radius = 90.0f, .intensity = 1.0f,
		.falloff = 1.0f } };
	const std::array circles{ ax::RadianceCircle{ .centre = { 0.0f, 0.0f }, .radius = 14.0f } };
	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, radiance_settings(), lights, {}, 1, centred_view(), {}, {},
		nullptr, {}, circles) };
	const auto litSide{ image.at(at(-10.0f), at(0.0f))[0] };
	const auto farSide{ image.at(at(10.0f), at(0.0f))[0] };
	EXPECT_GT(litSide, 60);
	EXPECT_GT(litSide, farSide + 40) << litSide << " " << farSide;
}

TEST_F(LightRendererTest, RadianceLeavesLightsWithoutShadowsAnalytic)
{
	const std::array lights{ ax::Light2D{ .position = { -36.0f, 0.0f }, .radius = 80.0f, .intensity = 1.0f,
		.falloff = 1.0f, .shadows = 0.0f } };
	const std::array circles{ ax::RadianceCircle{ .centre = { -12.0f, 0.0f }, .radius = 6.0f } };
	ax::LightRenderer::Stats stats{};
	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, radiance_settings(), lights, {}, 1, centred_view(), {}, {},
		nullptr, {}, circles, &stats) };
	EXPECT_EQ(stats.radianceLights, 0u);
	EXPECT_EQ(stats.lights, 1u);
	auto plain{ radiance_settings() };
	plain.radiance.enabled = false;
	const auto analytic{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, plain, lights) };
	EXPECT_TRUE(close_to(image.at(at(20.0f), at(0.0f)), analytic.at(at(20.0f), at(0.0f)), 3));
}

TEST_F(LightRendererTest, RadianceSkyOcclusionDarkensAmbientNearOccluders)
{
	auto lighting{ radiance_settings() };
	lighting.ambient = glm::vec3{ 0.5f };
	lighting.radiance.aoStrength = 1.0f;
	lighting.radiance.aoRange = 24.0f;
	const std::array circles{ ax::RadianceCircle{ .centre = { 0.0f, 0.0f }, .radius = 10.0f } };
	const auto image{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, {}, {}, 1, centred_view(), {}, {}, nullptr, {},
		circles) };
	const auto distant{ image.at(at(-50.0f), at(-50.0f))[0] };
	const auto beside{ image.at(at(13.0f), at(0.0f))[0] };
	EXPECT_NEAR(distant, 128, 4);
	EXPECT_LT(beside, distant - 15) << beside;
}

TEST_F(LightRendererTest, DISABLED_RadianceProfileDump)
{
	// Prints traced against analytic light along an axis and the diagonal, for tuning.
	for (const auto spacing : { 1u, 2u })
	{
		// AXENG_RADIANCE_DUMP_SOURCE sets the source radius; AXENG_RADIANCE_DUMP_NO_FIX turns the bilinear fix off.
		const auto environment{ [](const char* name)
		{
			char* value{};
			std::size_t length{};
			const std::string text{ _dupenv_s(&value, &length, name) == 0 && value != nullptr ? value : "" };
			std::free(value);
			return text;
		} };
		const auto sourceText{ environment("AXENG_RADIANCE_DUMP_SOURCE") };
		const std::array lights{ ax::Light2D{ .position = { 0.0f, 0.0f }, .radius = 48.0f, .intensity = 1.0f,
			.sourceRadius = sourceText.empty() ? 0.0f : std::stof(sourceText) } };
		auto lighting{ radiance_settings() };
		lighting.radiance.probeSpacing = spacing;
		lighting.radiance.bilinearFix = environment("AXENG_RADIANCE_DUMP_NO_FIX").empty();
		const auto traced{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, lights) };
		auto plain{ radiance_settings() };
		plain.radiance.enabled = false;
		const auto analytic{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, plain, lights) };
		std::printf("spacing %u\n", spacing);
		for (int d = 0; d < 48; d += 2)
			std::printf("%d: %d %d | %d %d\n", d, traced.at(at(static_cast<float>(d)), at(0.0f))[0], analytic.at(at(static_cast<float>(d)), at(0.0f))[0],
				traced.at(at(static_cast<float>(d) * 0.7071f), at(static_cast<float>(d) * 0.7071f))[0],
				analytic.at(at(static_cast<float>(d) * 0.7071f), at(static_cast<float>(d) * 0.7071f))[0]);
	}
}

TEST_F(LightRendererTest, DISABLED_ReportsGpuTimeForRadianceCascades)
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
	// The game's default zoom, 32 pixel tiles.
	const ax::LightView camera{ .viewport = { static_cast<float>(width), static_cast<float>(height) }, .zoom = 1.5f };
	std::vector<ax::Light2D> lights{};
	std::vector<ax::RadianceCircle> circles{};
	for (std::uint32_t index{}; index < 60; ++index)
		lights.push_back({ .position = { static_cast<float>((index * 7919u) % 1600u) - 800.0f,
			static_cast<float>((index * 104729u) % 900u) - 450.0f }, .radius = 320.0f, .intensity = 0.5f,
			.colour = { 1.0f, 0.9f, 0.7f }, .falloff = 1.6f, .sourceRadius = 16.0f });
	for (std::uint32_t index{}; index < 800; ++index)
		circles.push_back({ .centre = { static_cast<float>((index * 3571u) % 1900u) - 950.0f,
			static_cast<float>((index * 6151u) % 1100u) - 550.0f }, .radius = 13.0f });
	// The player's lamp and torch (64 pixel tiles), marched.
	auto withPlayer{ lights };
	withPlayer.push_back({ .radius = 2.6f * 64.0f, .intensity = 0.75f, .falloff = 1.5f, .sourceRadius = 19.2f,
		.shadows = ax::Light2D::marched_shadows });
	withPlayer.push_back({ .radius = 9.0f * 64.0f, .intensity = 0.85f, .falloff = 1.2f, .direction = { 0.96f, 0.28f },
		.coneInner = std::cos(0.30f), .coneOuter = std::cos(0.55f), .sourceRadius = 19.2f,
		.shadows = ax::Light2D::marched_shadows });
	// A lamp is nudged every frame so the cascades are traced afresh (the full cost), except in the "unchanged" case,
	// which shows what a frame costs when they are reused.
	struct Case { const char* name; float divisor; std::uint32_t spacing; bool fix; bool enabled; bool player{};
		bool still{}; std::uint32_t fixCascades{ 8 }; };
	for (const auto& test : { Case{ "off", 2, 2, true, false }, Case{ "low", 4, 2, true, true },
		Case{ "medium", 2, 2, true, true }, Case{ "medium, no bilinear fix", 2, 2, false, true },
		Case{ "high", 1.4142f, 2, true, true }, Case{ "ultra", 2, 1, true, true },
		Case{ "ultra, no bilinear fix", 2, 1, false, true },
		Case{ "low, player marched", 4, 2, true, true, true }, Case{ "medium, player marched", 2, 2, true, true, true },
		Case{ "medium, unchanged", 2, 2, true, true, false, true } })
	{
		auto settings{ ax::LightingSettings{ .enabled = true, .ambient = glm::vec3{ 0.12f }, .resolutionDivisor = 2 } };
		settings.radiance = { .enabled = test.enabled, .divisor = test.divisor, .probeSpacing = test.spacing,
			.aoStrength = 0.4f, .aoRange = 40.0f, .bilinearFix = test.fix, .fixCascades = test.fixCascades };
		int nudge{};
		const auto frame{ [&]
		{
			if (!test.still)
			{
				lights[0].position.x += (++nudge % 2 == 0) ? 0.5f : -0.5f;
				withPlayer[0].position = lights[0].position;
			}
			auto encoder{ s_device.CreateCommandEncoder() };
			renderer.encode(s_device, s_device.GetQueue(), encoder, view, width, height, camera, settings,
				test.player ? std::span<const ax::Light2D>{ withPlayer } : std::span<const ax::Light2D>{ lights }, {}, {},
				nullptr, {}, circles);
			const auto commands{ encoder.Finish() };
			s_device.GetQueue().Submit(1, &commands);
			renderer.submitted();
			s_instance.WaitAny(s_device.GetQueue().OnSubmittedWorkDone(wgpu::CallbackMode::WaitAnyOnly,
				[](wgpu::QueueWorkDoneStatus, wgpu::StringView) {}), UINT64_MAX);
		} };
		frame();
		constexpr int frames{ 30 };
		const auto started{ std::chrono::steady_clock::now() };
		for (int index{}; index < frames; ++index) frame();
		const auto elapsed{ std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() };
		const auto stats{ renderer.last_stats() };
		std::printf("%s: %.3f ms per frame (grid %ux%u, %u cascades, %u traced%s)%c", test.name, elapsed / frames,
			stats.radianceWidth, stats.radianceHeight, stats.radianceCascades, stats.radianceLights,
			stats.radianceReused ? ", reused" : "", 10);
		// The GPU's own time per stage, where timestamp queries exist.
		for (const auto& timing : renderer.gpu_timings())
			std::printf("    %s %.3f%c", timing.category + 8, timing.milliseconds, 10);
	}
}

TEST_F(LightRendererTest, RadianceBounceReflectsLightOffLitOccluders)
{
	const std::array lights{ ax::Light2D{ .position = { -44.0f, 0.0f }, .radius = 120.0f, .intensity = 0.3f,
		.falloff = 1.0f, .sourceRadius = 4.0f } };
	const std::array circles{ ax::RadianceCircle{ .centre = { 10.0f, 0.0f }, .radius = 16.0f,
		.albedo = { 1.0f, 1.0f, 1.0f, 1.0f } } };
	auto lighting{ radiance_settings() };
	lighting.ambient = glm::vec3{ 0.0f };
	const auto plain{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, lights, {}, 1, centred_view(), {}, {}, nullptr, {},
		circles, nullptr, {}, 2) };
	lighting.radiance.bounce = 1.0f;
	const auto bounced{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, lighting, lights, {}, 1, centred_view(), {}, {}, nullptr, {},
		circles, nullptr, {}, 2) };
	// Just off the circle's lit face, the light it reflects adds to the lamp's; behind it, in its shadow, nothing does.
	const auto litBefore{ plain.at(at(-12.0f), at(14.0f))[0] }, litAfter{ bounced.at(at(-12.0f), at(14.0f))[0] };
	EXPECT_GT(litAfter, litBefore + 3) << litBefore << " -> " << litAfter;
	EXPECT_NEAR(bounced.at(at(40.0f), at(0.0f))[0], plain.at(at(40.0f), at(0.0f))[0], 2);
}
