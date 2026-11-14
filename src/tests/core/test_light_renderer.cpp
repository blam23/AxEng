#include <gtest/gtest.h>
#include <dawn/native/DawnNative.h>
#include <windows.h>

#include "axeng/core/lighting/light_renderer.h"
#include "axeng/core/particles/particle_system.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cmath>
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
		std::span<const ax::Glow2D> glows = {}, std::uint32_t samples = 1, const ax::LightView& view = centred_view(),
		const std::function<void(wgpu::CommandEncoder&)>& before = {},
		const std::function<void(wgpu::RenderPassEncoder&, std::uint32_t)>& extra = {}, const ax::NoiseShadow2D* noiseShadow = nullptr,
		std::span<const ax::LightOccluder2D> occluders = {}, std::span<const ax::RadianceCircle> circles = {},
		ax::LightRenderer::Stats* stats = nullptr, const ax::RadianceCascades::OccluderDraw& radianceOccluders = {})
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
		EXPECT_EQ(stats.lights, 0u);
		EXPECT_GT(stats.radianceCascades, 1u);
		// Coarser probes resolve a little less precisely.
		const int tolerance{ spacing == 1 ? 24 : 32 };
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
	for (const auto source : { 0.0f, 3.5f, 4.5f })
	{
		const std::array lights{ ax::Light2D{ .position = { 0.0f, 0.0f }, .radius = 48.0f, .intensity = 1.0f, .sourceRadius = source } };
		const auto traced{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, radiance_settings(), lights) };
		auto plain{ radiance_settings() };
		plain.radiance.enabled = false;
		const auto analytic{ render({ 1.0f, 1.0f, 1.0f, 1.0f }, plain, lights) };
		std::printf("source %g\n", source);
		for (int d = 0; d < 48; d += 3)
			std::printf("%d: %d %d | %d\n", d, traced.at(at(static_cast<float>(d)), at(0.0f))[0], analytic.at(at(static_cast<float>(d)), at(0.0f))[0],
				traced.at(at(static_cast<float>(d) * 0.7071f), at(static_cast<float>(d) * 0.7071f))[0]);
	}
}
