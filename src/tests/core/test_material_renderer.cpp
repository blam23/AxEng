#include <gtest/gtest.h>
#include <dawn/native/DawnNative.h>
#include <windows.h>

#include "axeng/core/materials/material_renderer.h"
#include "axeng/core/shapes/shape_renderer.h"
#include "tests/main/log_capture.h"

#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace
{
	constexpr std::uint32_t s_size{ 64 };
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

	testing::AssertionResult near_pixel(const Image& image, std::uint32_t x, std::uint32_t y, Pixel expected, int tolerance = 3)
	{
		const auto actual{ image.at(x, y) };
		for (std::size_t i = 0; i < 4; ++i)
			if (std::abs(actual[i] - expected[i]) > tolerance)
				return testing::AssertionFailure() << "pixel (" << x << ", " << y << ") is (" << actual[0] << ", " << actual[1]
					<< ", " << actual[2] << ", " << actual[3] << ") expected (" << expected[0] << ", " << expected[1] << ", "
					<< expected[2] << ", " << expected[3] << ")";
		return testing::AssertionSuccess();
	}

	ax::MaterialView screen_view(double time = 0.0)
	{
		return { .viewport = { static_cast<float>(s_size), static_cast<float>(s_size) }, .time = time };
	}

	ax::ShapeView shape_view()
	{
		return { .viewport = { static_cast<float>(s_size), static_cast<float>(s_size) } };
	}
}

class MaterialRendererTest : public testing::Test
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

		wgpu::Limits limits{};
		limits.maxBindGroups = 2;
		limits.maxVertexBuffers = 1;
		limits.maxBufferSize = 150000 * sizeof(wgpu::VertexAttribute);
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
		ax::MaterialRenderer::shutdown();
		ax::ShapeRenderer::shutdown();
		s_device = nullptr;
		s_instance = nullptr;
	}

	void SetUp() override
	{
		if (s_device == nullptr)
			GTEST_SKIP() << "No D3D12 WebGPU device is available";
		ASSERT_TRUE(ax::ShapeRenderer::init(s_device, s_shapeConfig));
		ASSERT_TRUE(ax::MaterialRenderer::init(s_device, s_config));
	}

	void TearDown() override
	{
		ax::MaterialRenderer::shutdown();
		ax::ShapeRenderer::shutdown();
	}

	static Image render(const std::function<void(wgpu::RenderPassEncoder&)>& record, const ax::MaterialView& view = screen_view())
	{
		const auto makeTexture = [](wgpu::TextureFormat format, std::uint32_t samples, wgpu::TextureUsage usage)
		{
			wgpu::TextureDescriptor desc{};
			desc.size = { s_size, s_size };
			desc.format = format;
			desc.sampleCount = samples;
			desc.usage = usage;
			return s_device.CreateTexture(&desc);
		};
		const auto msaa{ makeTexture(s_config.colourFormat, s_config.sampleCount, wgpu::TextureUsage::RenderAttachment) };
		const auto resolve{ makeTexture(s_config.colourFormat, 1, wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc) };
		const auto depth{ makeTexture(s_config.depthFormat, s_config.sampleCount, wgpu::TextureUsage::RenderAttachment) };

		constexpr std::uint32_t bytesPerRow{ s_size * 4 };
		wgpu::BufferDescriptor readbackDesc{};
		readbackDesc.size = bytesPerRow * s_size;
		readbackDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
		const auto readback{ s_device.CreateBuffer(&readbackDesc) };

		s_device.PushErrorScope(wgpu::ErrorFilter::Validation);
		ax::ShapeRenderer::begin_frame(shape_view());
		ax::MaterialRenderer::begin_frame(view);
		auto encoder{ s_device.CreateCommandEncoder() };

		wgpu::RenderPassColorAttachment colour{};
		colour.view = msaa.CreateView();
		colour.resolveTarget = resolve.CreateView();
		colour.loadOp = wgpu::LoadOp::Clear;
		colour.storeOp = wgpu::StoreOp::Store;
		colour.clearValue = { 0.0, 0.0, 0.0, 1.0 };
		wgpu::RenderPassDepthStencilAttachment depthAttachment{};
		depthAttachment.view = depth.CreateView();
		depthAttachment.depthLoadOp = wgpu::LoadOp::Clear;
		depthAttachment.depthStoreOp = wgpu::StoreOp::Store;
		depthAttachment.depthClearValue = 0.0f;
		wgpu::RenderPassDescriptor passDesc{};
		passDesc.colorAttachmentCount = 1;
		passDesc.colorAttachments = &colour;
		passDesc.depthStencilAttachment = &depthAttachment;
		auto pass{ encoder.BeginRenderPass(&passDesc) };
		record(pass);
		pass.End();

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

	static ax::MaterialDraw quad_draw(const std::string& material, glm::vec2 position, glm::vec2 size, float z,
		std::vector<glm::vec4> params = {})
	{
		ax::MaterialDraw draw{};
		draw.material = material;
		draw.quads.push_back({ .position = position, .size = size, .z = z });
		draw.params = std::move(params);
		draw.screenSpace = true;
		return draw;
	}

	static constexpr ax::MaterialRendererConfig s_config{
		.colourFormat = wgpu::TextureFormat::RGBA8Unorm,
		.depthFormat = wgpu::TextureFormat::Depth24Plus,
		.sampleCount = 4,
	};
	static constexpr ax::ShapeRendererConfig s_shapeConfig{
		.colourFormat = wgpu::TextureFormat::RGBA8Unorm,
		.depthFormat = wgpu::TextureFormat::Depth24Plus,
		.sampleCount = 4,
	};

	static inline wgpu::Instance s_instance{};
	static inline wgpu::Device s_device{};
};

TEST_F(MaterialRendererTest, AMaterialFillsItsQuadWithItsParameters)
{
	ASSERT_TRUE(ax::MaterialRenderer::define({ .name = "solid", .fragment = "fn material(in : MaterialInput) -> vec4<f32> { return params[0]; }" }));
	const auto image{ render([](wgpu::RenderPassEncoder& pass)
		{
			ax::MaterialRenderer::draw(pass, quad_draw("solid", { 16, 16 }, { 32, 32 }, 0.0f, { { 1.0f, 0.0f, 0.0f, 1.0f } }));
		}) };
	EXPECT_TRUE(near_pixel(image, 32, 32, { 255, 0, 0, 255 }));
	EXPECT_TRUE(near_pixel(image, 8, 8, { 0, 0, 0, 255 }));
	EXPECT_TRUE(near_pixel(image, 56, 56, { 0, 0, 0, 255 }));
	EXPECT_EQ(ax::MaterialRenderer::last_skipped(), 0u);
}

TEST_F(MaterialRendererTest, TheFragmentSeesItsPositionUvDataAndTime)
{
	ASSERT_TRUE(ax::MaterialRenderer::define({ .name = "inputs", .fragment = R"(
		fn material(in : MaterialInput) -> vec4<f32> {
			return vec4<f32>(in.uv.x, in.data0.x, in.time / 10.0, 1.0);
		})" }));
	ax::MaterialDraw draw{};
	draw.material = "inputs";
	draw.screenSpace = true;
	draw.quads.push_back({ .position = { 0, 0 }, .size = { 64, 64 }, .z = 0.0f, .data0 = { 0.5f, 0, 0, 0 } });
	const auto image{ render([&draw](wgpu::RenderPassEncoder& pass) { ax::MaterialRenderer::draw(pass, draw); }, screen_view(5.0)) };
	// uv.x runs from 0 to 1 across the quad, data0 is the same everywhere and time arrives as seconds.
	EXPECT_TRUE(near_pixel(image, 4, 32, { 16, 128, 128, 255 }, 6));
	EXPECT_TRUE(near_pixel(image, 59, 32, { 238, 128, 128, 255 }, 6));
}

TEST_F(MaterialRendererTest, AlphaBlendsOverWhatIsThere)
{
	ASSERT_TRUE(ax::MaterialRenderer::define({ .name = "glass", .fragment = "fn material(in : MaterialInput) -> vec4<f32> { return vec4<f32>(0.0, 0.0, 1.0, 0.5); }" }));
	ax::ShapeList background{};
	ax::Rectangle red{};
	red.position = { 0, 0 };
	red.size = { 64, 64 };
	red.fill = { 1.0f, 0.0f, 0.0f, 1.0f };
	red.z = 0.0f;
	background.emplace_back(red);
	const auto image{ render([&background](wgpu::RenderPassEncoder& pass)
		{
			ax::ShapeRenderer::draw(pass, background);
			ax::MaterialRenderer::draw(pass, quad_draw("glass", { 0, 0 }, { 64, 64 }, 1.0f));
		}) };
	EXPECT_TRUE(near_pixel(image, 32, 32, { 128, 0, 128, 255 }, 4));
}

TEST_F(MaterialRendererTest, DepthDecidesWhatCoversWhat)
{
	ASSERT_TRUE(ax::MaterialRenderer::define({ .name = "green", .fragment = "fn material(in : MaterialInput) -> vec4<f32> { return vec4<f32>(0.0, 1.0, 0.0, 1.0); }" }));
	ax::ShapeList shapes{};
	ax::Rectangle red{};
	red.position = { 0, 0 };
	red.size = { 64, 64 };
	red.fill = { 1.0f, 0.0f, 0.0f, 1.0f };
	red.z = 2.0f;
	shapes.emplace_back(red);
	const auto behind{ render([&shapes](wgpu::RenderPassEncoder& pass)
		{
			ax::ShapeRenderer::draw(pass, shapes);
			ax::MaterialRenderer::draw(pass, quad_draw("green", { 0, 0 }, { 64, 64 }, 1.0f));
		}) };
	EXPECT_TRUE(near_pixel(behind, 32, 32, { 255, 0, 0, 255 }));
	const auto in_front{ render([&shapes](wgpu::RenderPassEncoder& pass)
		{
			ax::ShapeRenderer::draw(pass, shapes);
			ax::MaterialRenderer::draw(pass, quad_draw("green", { 0, 0 }, { 64, 64 }, 3.0f));
		}) };
	EXPECT_TRUE(near_pixel(in_front, 32, 32, { 0, 255, 0, 255 }));
}

TEST_F(MaterialRendererTest, WorldSpaceDrawsFollowTheCameraAndScreenSpaceOnesDoNot)
{
	ASSERT_TRUE(ax::MaterialRenderer::define({ .name = "white", .fragment = "fn material(in : MaterialInput) -> vec4<f32> { return vec4<f32>(1.0); }" }));
	auto view{ screen_view() };
	view.cameraPosition = { 100.0f, 100.0f };
	// The camera looks at (100, 100), so a world quad there is in the middle of the view.
	auto world{ quad_draw("white", { 90, 90 }, { 20, 20 }, 0.0f) };
	world.screenSpace = false;
	const auto screen{ quad_draw("white", { 0, 0 }, { 10, 10 }, 0.0f) };
	const auto image{ render([&](wgpu::RenderPassEncoder& pass)
		{
			ax::MaterialRenderer::draw(pass, world);
			ax::MaterialRenderer::draw(pass, screen);
		}, view) };
	EXPECT_TRUE(near_pixel(image, 32, 32, { 255, 255, 255, 255 }));
	EXPECT_TRUE(near_pixel(image, 4, 4, { 255, 255, 255, 255 }));
	EXPECT_TRUE(near_pixel(image, 50, 50, { 0, 0, 0, 255 }));
}

TEST_F(MaterialRendererTest, ManyDrawsInOneFrameEachKeepTheirOwnParameters)
{
	ASSERT_TRUE(ax::MaterialRenderer::define({ .name = "tint", .fragment = "fn material(in : MaterialInput) -> vec4<f32> { return params[3]; }" }));
	const auto image{ render([](wgpu::RenderPassEncoder& pass)
		{
			for (std::uint32_t i = 0; i < 4; ++i)
				ax::MaterialRenderer::draw(pass, quad_draw("tint", { static_cast<float>(i) * 16, 0 }, { 16, 64 }, 0.0f,
					{ {}, {}, {}, { static_cast<float>(i) / 3.0f, 0.0f, 0.0f, 1.0f } }));
		}) };
	for (std::uint32_t i = 0; i < 4; ++i)
		EXPECT_TRUE(near_pixel(image, i * 16 + 8, 32, { static_cast<int>(std::lround(255.0f * static_cast<float>(i) / 3.0f)), 0, 0, 255 }, 3));
}

TEST_F(MaterialRendererTest, UnknownMaterialsAreSkippedAndCounted)
{
	// The first draw of a material that was never defined logs once.
	testlog::LogCapture::instance().expect_total_error_count(1);
	const auto image{ render([](wgpu::RenderPassEncoder& pass)
		{
			ax::MaterialRenderer::draw(pass, quad_draw("nobody", { 0, 0 }, { 64, 64 }, 0.0f));
		}) };
	EXPECT_TRUE(near_pixel(image, 32, 32, { 0, 0, 0, 255 }));
	ax::MaterialRenderer::begin_frame(screen_view());
	EXPECT_EQ(ax::MaterialRenderer::last_skipped(), 1u);
	EXPECT_FALSE(ax::MaterialRenderer::define({ .name = "", .fragment = "x" }));
	EXPECT_FALSE(ax::MaterialRenderer::define({ .name = "empty", .fragment = "" }));
}
