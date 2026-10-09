#include <gtest/gtest.h>
#include <dawn/native/DawnNative.h>
#include <windows.h>

#include "axeng/core/lua/bindings/lua_shape_bindings.h"
#include "axeng/core/lua/bindings/lua_vector_bindings.h"
#include "axeng/core/shapes/debug_shapes.h"
#include "axeng/core/shapes/shape_renderer.h"
#include "tests/main/log_capture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <numbers>
#include <string>
#include <vector>

namespace
{
	constexpr std::uint32_t s_size{ 128 };
	constexpr float s_half_pi{ std::numbers::pi_v<float> * 0.5f };

	constexpr glm::vec4 s_red{ 1.0f, 0.0f, 0.0f, 1.0f };
	constexpr glm::vec4 s_green{ 0.0f, 1.0f, 0.0f, 1.0f };
	constexpr glm::vec4 s_blue{ 0.0f, 0.0f, 1.0f, 1.0f };
	constexpr glm::vec4 s_black{ 0.0f, 0.0f, 0.0f, 1.0f };
	constexpr glm::vec4 s_transparent{ 0.0f };

	using Pixel = std::array<int, 4>;

	struct Image
	{
		std::uint32_t width{};
		std::uint32_t height{};
		std::vector<std::uint8_t> rgba;

		// Pixel containing the given point.
		Pixel at(float x, float y) const
		{
			const auto px{ static_cast<std::uint32_t>(std::floor(x)) };
			const auto py{ static_cast<std::uint32_t>(std::floor(y)) };
			const auto* p{ &rgba[(py * width + px) * 4] };
			return { p[0], p[1], p[2], p[3] };
		}

		bool operator==(const Image&) const = default;
	};

	Pixel to_pixel(const glm::vec4& c)
	{
		return {
			static_cast<int>(std::lround(c.r * 255.0f)), static_cast<int>(std::lround(c.g * 255.0f)),
			static_cast<int>(std::lround(c.b * 255.0f)), static_cast<int>(std::lround(c.a * 255.0f)) };
	}

	testing::AssertionResult pixel_near(const Image& image, glm::vec2 point, const Pixel& expected, int tolerance)
	{
		const auto actual{ image.at(point.x, point.y) };
		for (std::size_t i = 0; i < 4; ++i)
		{
			if (std::abs(actual[i] - expected[i]) > tolerance)
			{
				return testing::AssertionFailure()
					<< "pixel at (" << point.x << ", " << point.y << ") is ("
					<< actual[0] << ", " << actual[1] << ", " << actual[2] << ", " << actual[3] << ") expected ("
					<< expected[0] << ", " << expected[1] << ", " << expected[2] << ", " << expected[3] << ")";
			}
		}
		return testing::AssertionSuccess();
	}

	ax::ShapeView screen_view()
	{
		return { .viewport = { static_cast<float>(s_size), static_cast<float>(s_size) } };
	}
}

#define EXPECT_PIXEL(image, x, y, colour) EXPECT_TRUE(pixel_near(image, { x, y }, to_pixel(colour), 1))
#define EXPECT_PIXEL_NEAR(image, x, y, pixel, tol) EXPECT_TRUE(pixel_near(image, { x, y }, pixel, tol))

class ShapeRendererTest : public testing::Test
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
		limits.maxBufferSize = 150000 * sizeof(wgpu::VertexAttribute);
		limits.maxVertexAttributes = 4;

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
		ax::ShapeRenderer::shutdown();
		s_device = nullptr;
		s_instance = nullptr;
	}

	void SetUp() override
	{
		if (s_device == nullptr)
			GTEST_SKIP() << "No D3D12 WebGPU device is available";
		ASSERT_TRUE(ax::ShapeRenderer::init(s_device, s_config));
	}

	void TearDown() override
	{
		ax::ShapeRenderer::shutdown();
	}

	// Renders each list with its own ShapeRenderer::draw call into a single render pass.
	static Image render(const std::vector<ax::ShapeList>& drawCalls, const ax::ShapeView& view = screen_view())
	{
		return render_pass([&drawCalls](wgpu::RenderPassEncoder& pass)
			{
				for (const auto& shapes : drawCalls)
					ax::ShapeRenderer::draw(pass, shapes);
			}, view);
	}

	// Renders whatever `record` draws into a single offscreen render pass and reads it back.
	static Image render_pass(const std::function<void(wgpu::RenderPassEncoder&)>& record, const ax::ShapeView& view = screen_view())
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

		ax::ShapeRenderer::begin_frame(view);
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

		Image image{ s_size, s_size, std::vector<std::uint8_t>(readbackDesc.size) };
		if (mapped)
		{
			std::memcpy(image.rgba.data(), readback.GetConstMappedRange(), readbackDesc.size);
			readback.Unmap();
		}
		return image;
	}

	static Image render(const ax::ShapeList& shapes, const ax::ShapeView& view = screen_view())
	{
		return render(std::vector<ax::ShapeList>{ shapes }, view);
	}

	static constexpr ax::ShapeRendererConfig s_config{
		.colourFormat = wgpu::TextureFormat::RGBA8Unorm,
		.depthFormat = wgpu::TextureFormat::Depth24Plus,
		.sampleCount = 4,
	};

	static inline wgpu::Instance s_instance{};
	static inline wgpu::Device s_device{};
};

//
// Rectangles
//

TEST_F(ShapeRendererTest, RectangleFillsExactPixelArea)
{
	const auto image{ render({ ax::Rectangle{ .position = { 32, 32 }, .size = { 64, 32 }, .fill = s_red, .screenSpace = true } }) };

	EXPECT_PIXEL(image, 32, 32, s_red);
	EXPECT_PIXEL(image, 95, 63, s_red);
	EXPECT_PIXEL(image, 64, 48, s_red);
	EXPECT_PIXEL(image, 31, 48, s_black);
	EXPECT_PIXEL(image, 96, 48, s_black);
	EXPECT_PIXEL(image, 64, 31, s_black);
	EXPECT_PIXEL(image, 64, 64, s_black);
}

TEST_F(ShapeRendererTest, RectangleOutlineIsDrawnInsideBoundary)
{
	const auto image{ render({ ax::Rectangle{
		.position = { 32, 32 }, .size = { 64, 64 }, .fill = s_red,
		.outline = { .colour = s_blue, .thickness = 4 }, .screenSpace = true } }) };

	EXPECT_PIXEL(image, 32, 64, s_blue);
	EXPECT_PIXEL(image, 35, 64, s_blue);
	EXPECT_PIXEL(image, 95, 64, s_blue);
	EXPECT_PIXEL(image, 64, 32, s_blue);
	EXPECT_PIXEL(image, 64, 95, s_blue);
	EXPECT_PIXEL(image, 36, 64, s_red);
	EXPECT_PIXEL(image, 64, 64, s_red);
	EXPECT_PIXEL(image, 31, 64, s_black);
	EXPECT_PIXEL(image, 96, 64, s_black);
}

TEST_F(ShapeRendererTest, RectangleRotatesAroundCentre)
{
	const ax::Rectangle rect{ .position = { 44, 24 }, .size = { 40, 80 }, .fill = s_red, .screenSpace = true };

	const auto upright{ render({ rect }) };
	EXPECT_PIXEL(upright, 64, 30, s_red);
	EXPECT_PIXEL(upright, 30, 64, s_black);

	auto rotated{ rect };
	rotated.rotation = s_half_pi;
	const auto image{ render({ rotated }) };
	EXPECT_PIXEL(image, 64, 64, s_red);
	EXPECT_PIXEL(image, 30, 64, s_red);
	EXPECT_PIXEL(image, 98, 64, s_red);
	EXPECT_PIXEL(image, 64, 30, s_black);
	EXPECT_PIXEL(image, 64, 98, s_black);
}

TEST_F(ShapeRendererTest, RoundedRectangleCutsCorners)
{
	const auto image{ render({ ax::Rectangle{
		.position = { 32, 32 }, .size = { 64, 64 }, .cornerRadius = 16, .fill = s_red, .screenSpace = true } }) };

	EXPECT_PIXEL(image, 33, 33, s_black);
	EXPECT_PIXEL(image, 94, 94, s_black);
	EXPECT_PIXEL(image, 64, 33, s_red);
	EXPECT_PIXEL(image, 33, 64, s_red);
	EXPECT_PIXEL(image, 40, 40, s_red); // inside the corner arc
}

//
// Circles and ellipses
//

TEST_F(ShapeRendererTest, CircleCoversRadius)
{
	const auto image{ render({ ax::Circle{ .position = { 64, 64 }, .radius = 20, .fill = s_red, .screenSpace = true } }) };

	EXPECT_PIXEL(image, 64, 64, s_red);
	EXPECT_PIXEL(image, 64 + 18, 64, s_red);
	EXPECT_PIXEL(image, 64 - 18, 64, s_red);
	EXPECT_PIXEL(image, 64, 64 - 18, s_red);
	EXPECT_PIXEL(image, 64 + 12, 64 + 12, s_red);
	EXPECT_PIXEL(image, 64 + 22, 64, s_black);
	EXPECT_PIXEL(image, 64, 64 + 22, s_black);
	EXPECT_PIXEL(image, 64 + 15.5f, 64 + 15.5f, s_black);
}

TEST_F(ShapeRendererTest, CircleOutline)
{
	const auto image{ render({ ax::Circle{
		.position = { 64, 64 }, .radius = 20, .fill = s_red,
		.outline = { .colour = s_green, .thickness = 5 }, .screenSpace = true } }) };

	EXPECT_PIXEL(image, 64 + 17, 64, s_green);
	EXPECT_PIXEL(image, 64, 64 - 18, s_green);
	EXPECT_PIXEL(image, 64 + 12, 64, s_red);
	EXPECT_PIXEL(image, 64, 64, s_red);
	EXPECT_PIXEL(image, 64 + 22, 64, s_black);
}

TEST_F(ShapeRendererTest, OutlineThickerThanShapeFillsWithOutlineColour)
{
	const auto image{ render({ ax::Circle{
		.position = { 64, 64 }, .radius = 10, .fill = s_red,
		.outline = { .colour = s_blue, .thickness = 15 }, .screenSpace = true } }) };

	EXPECT_PIXEL(image, 64, 64, s_blue);
	EXPECT_PIXEL(image, 64 + 8, 64, s_blue);
	EXPECT_PIXEL(image, 64 + 12, 64, s_black);
}

TEST_F(ShapeRendererTest, EllipseUsesBothRadiiAndRotation)
{
	const ax::Ellipse ellipse{ .position = { 64, 64 }, .radii = { 40, 15 }, .fill = s_red, .screenSpace = true };

	const auto image{ render({ ellipse }) };
	EXPECT_PIXEL(image, 64 + 37, 64, s_red);
	EXPECT_PIXEL(image, 64 - 37, 64, s_red);
	EXPECT_PIXEL(image, 64, 64 + 12, s_red);
	EXPECT_PIXEL(image, 64, 64 + 18, s_black);
	EXPECT_PIXEL(image, 64 + 43, 64, s_black);

	auto rotated{ ellipse };
	rotated.rotation = s_half_pi;
	const auto rotatedImage{ render({ rotated }) };
	EXPECT_PIXEL(rotatedImage, 64, 64 + 37, s_red);
	EXPECT_PIXEL(rotatedImage, 64 + 18, 64, s_black);
}

//
// Triangles and polygons
//

TEST_F(ShapeRendererTest, TriangleFillsBetweenPoints)
{
	const ax::Triangle triangle{ .position = { 20, 20 }, .points = { { { 0, 0 }, { 60, 0 }, { 0, 60 } } }, .fill = s_red, .screenSpace = true };
	const auto image{ render({ triangle }) };

	EXPECT_PIXEL(image, 25, 25, s_red);
	EXPECT_PIXEL(image, 70, 22, s_red);
	EXPECT_PIXEL(image, 22, 70, s_red);
	EXPECT_PIXEL(image, 55, 55, s_black);
	EXPECT_PIXEL(image, 18, 40, s_black);

	// Winding order does not matter.
	auto reversed{ triangle };
	std::swap(reversed.points[1], reversed.points[2]);
	EXPECT_EQ(render({ reversed }), image);
}

TEST_F(ShapeRendererTest, TriangleScalesAndRotatesAroundPosition)
{
	const ax::Triangle triangle{
		.position = { 64, 64 }, .points = { { { 0, 0 }, { 20, 0 }, { 0, 5 } } }, .scale = { 2, 2 },
		.fill = s_red, .screenSpace = true };

	const auto image{ render({ triangle }) };
	EXPECT_PIXEL(image, 74, 67, s_red);
	EXPECT_PIXEL(image, 61, 74, s_black);

	auto rotated{ triangle };
	rotated.rotation = s_half_pi;
	const auto rotatedImage{ render({ rotated }) };
	EXPECT_PIXEL(rotatedImage, 61, 74, s_red);
	EXPECT_PIXEL(rotatedImage, 74, 67, s_black);
}

TEST_F(ShapeRendererTest, ConcavePolygonLeavesNotchEmpty)
{
	const ax::Polygon polygon{
		.position = { 20, 20 },
		.points = { { 0, 0 }, { 60, 0 }, { 60, 20 }, { 20, 20 }, { 20, 60 }, { 0, 60 } },
		.fill = s_red, .screenSpace = true };

	const auto image{ render({ polygon }) };
	EXPECT_PIXEL(image, 30, 30, s_red);
	EXPECT_PIXEL(image, 75, 30, s_red);
	EXPECT_PIXEL(image, 30, 75, s_red);
	EXPECT_PIXEL(image, 60, 60, s_black);
	EXPECT_PIXEL(image, 45, 45, s_black);

	auto reversed{ polygon };
	std::reverse(reversed.points.begin(), reversed.points.end());
	EXPECT_EQ(render({ reversed }), image);
}

TEST_F(ShapeRendererTest, ConcavePolygonOutline)
{
	const auto image{ render({ ax::Polygon{
		.position = { 20, 20 },
		.points = { { 0, 0 }, { 60, 0 }, { 60, 20 }, { 20, 20 }, { 20, 60 }, { 0, 60 } },
		.fill = s_red, .outline = { .colour = s_blue, .thickness = 4 }, .screenSpace = true } }) };

	EXPECT_PIXEL(image, 21, 50, s_blue);
	EXPECT_PIXEL(image, 50, 21, s_blue);
	EXPECT_PIXEL(image, 38, 50, s_blue); // inner edge of the vertical arm
	EXPECT_PIXEL(image, 50, 38, s_blue); // inner edge of the horizontal arm
	EXPECT_PIXEL(image, 30, 30, s_red);
	EXPECT_PIXEL(image, 30, 70, s_red);
	EXPECT_PIXEL(image, 60, 60, s_black);
}

//
// Lines
//

TEST_F(ShapeRendererTest, LineButtCapEndsAtEndPoints)
{
	const auto image{ render({ ax::Line{
		.start = { 40, 64 }, .end = { 88, 64 }, .thickness = 16, .cap = ax::LineCap::Butt, .fill = s_red, .screenSpace = true } }) };

	EXPECT_PIXEL(image, 64, 64, s_red);
	EXPECT_PIXEL(image, 40, 57, s_red);
	EXPECT_PIXEL(image, 87, 71, s_red);
	EXPECT_PIXEL(image, 36, 64, s_black);
	EXPECT_PIXEL(image, 92, 64, s_black);
	EXPECT_PIXEL(image, 64, 74, s_black);
}

TEST_F(ShapeRendererTest, LineSquareCapExtendsByHalfThickness)
{
	const auto image{ render({ ax::Line{
		.start = { 40, 64 }, .end = { 88, 64 }, .thickness = 16, .cap = ax::LineCap::Square, .fill = s_red, .screenSpace = true } }) };

	EXPECT_PIXEL(image, 36, 64, s_red);
	EXPECT_PIXEL(image, 92, 64, s_red);
	EXPECT_PIXEL(image, 33, 57, s_red);
	EXPECT_PIXEL(image, 94, 70, s_red);
	EXPECT_PIXEL(image, 30, 64, s_black);
	EXPECT_PIXEL(image, 97, 64, s_black);
}

TEST_F(ShapeRendererTest, LineRoundCapIsSemicircle)
{
	const auto image{ render({ ax::Line{
		.start = { 40, 64 }, .end = { 88, 64 }, .thickness = 16, .cap = ax::LineCap::Round, .fill = s_red, .screenSpace = true } }) };

	EXPECT_PIXEL(image, 36, 64, s_red);
	EXPECT_PIXEL(image, 33, 64, s_red);
	EXPECT_PIXEL(image, 94, 64, s_red);
	EXPECT_PIXEL(image, 33, 57, s_black); // square corner is cut off
	EXPECT_PIXEL(image, 94, 70, s_black);
	EXPECT_PIXEL(image, 30, 64, s_black);
}

TEST_F(ShapeRendererTest, LineRotatesAroundMidpointWithOutline)
{
	const auto image{ render({ ax::Line{
		.start = { 40, 64 }, .end = { 88, 64 }, .thickness = 16, .fill = s_red,
		.outline = { .colour = s_blue, .thickness = 3 }, .rotation = s_half_pi, .screenSpace = true } }) };

	EXPECT_PIXEL(image, 64, 44, s_red);
	EXPECT_PIXEL(image, 64, 84, s_red);
	EXPECT_PIXEL(image, 57, 64, s_blue);
	EXPECT_PIXEL(image, 70, 64, s_blue);
	EXPECT_PIXEL(image, 64, 41, s_blue);
	EXPECT_PIXEL(image, 44, 64, s_black);
	EXPECT_PIXEL(image, 64, 36, s_black);
}

TEST_F(ShapeRendererTest, ZeroLengthLineCaps)
{
	const ax::Line dot{ .start = { 64, 64 }, .end = { 64, 64 }, .thickness = 16, .fill = s_red, .screenSpace = true };

	auto butt{ dot };
	EXPECT_PIXEL(render({ butt }), 64, 64, s_black);

	auto square{ dot };
	square.cap = ax::LineCap::Square;
	const auto squareImage{ render({ square }) };
	EXPECT_PIXEL(squareImage, 64, 64, s_red);
	EXPECT_PIXEL(squareImage, 57, 57, s_red);

	auto round{ dot };
	round.cap = ax::LineCap::Round;
	const auto roundImage{ render({ round }) };
	EXPECT_PIXEL(roundImage, 64, 64, s_red);
	EXPECT_PIXEL(roundImage, 57, 57, s_black);
}

//
// Ordering and blending
//

TEST_F(ShapeRendererTest, HigherZDrawsOnTopRegardlessOfSubmissionOrder)
{
	const ax::Rectangle red{ .position = { 20, 20 }, .size = { 60, 60 }, .fill = s_red, .z = 5, .screenSpace = true };
	const ax::Rectangle blue{ .position = { 50, 50 }, .size = { 60, 60 }, .fill = s_blue, .z = 1, .screenSpace = true };

	const auto image{ render({ red, blue }) };
	EXPECT_PIXEL(image, 65, 65, s_red);
	EXPECT_PIXEL(image, 100, 100, s_blue);
	EXPECT_EQ(render({ blue, red }), image);
}

TEST_F(ShapeRendererTest, EqualZUsesSubmissionOrder)
{
	const ax::Rectangle red{ .position = { 20, 20 }, .size = { 60, 60 }, .fill = s_red, .screenSpace = true };
	const ax::Rectangle blue{ .position = { 50, 50 }, .size = { 60, 60 }, .fill = s_blue, .screenSpace = true };

	EXPECT_PIXEL(render({ red, blue }), 65, 65, s_blue);
	EXPECT_PIXEL(render({ blue, red }), 65, 65, s_red);
}

TEST_F(ShapeRendererTest, ZIsRespectedAcrossSeparateDrawCalls)
{
	const ax::Rectangle red{ .position = { 20, 20 }, .size = { 60, 60 }, .fill = s_red, .z = 2, .screenSpace = true };
	const ax::Rectangle blue{ .position = { 50, 50 }, .size = { 60, 60 }, .fill = s_blue, .z = 1, .screenSpace = true };

	const auto image{ render(std::vector<ax::ShapeList>{ { red }, { blue } }) };
	EXPECT_PIXEL(image, 65, 65, s_red);
	EXPECT_PIXEL(image, 100, 100, s_blue);
}

TEST_F(ShapeRendererTest, TranslucentFillBlendsOverOpaque)
{
	const ax::Rectangle blue{ .position = { 0, 0 }, .size = { 128, 128 }, .fill = s_blue, .z = 0, .screenSpace = true };
	const ax::Rectangle red{ .position = { 32, 32 }, .size = { 64, 64 }, .fill = { 1, 0, 0, 0.5f }, .z = 1, .screenSpace = true };

	const Pixel expected{ 128, 0, 127, 255 };
	EXPECT_PIXEL_NEAR(render({ blue, red }), 64, 64, expected, 2);
	// Translucent shapes blend over opaque ones even when submitted first.
	const auto image{ render({ red, blue }) };
	EXPECT_PIXEL_NEAR(image, 64, 64, expected, 2);
	EXPECT_PIXEL(image, 10, 10, s_blue);
}

TEST_F(ShapeRendererTest, TranslucentShapesBlendInZOrder)
{
	const ax::Rectangle green{ .position = { 32, 32 }, .size = { 64, 64 }, .fill = { 0, 1, 0, 0.5f }, .z = 2, .screenSpace = true };
	const ax::Rectangle red{ .position = { 32, 32 }, .size = { 64, 64 }, .fill = { 1, 0, 0, 0.5f }, .z = 1, .screenSpace = true };

	const auto image{ render({ green, red }) };
	// Red over black, then green over that.
	EXPECT_PIXEL_NEAR(image, 64, 64, (Pixel{ 64, 128, 0, 255 }), 2);
	EXPECT_EQ(render({ red, green }), image);
}

TEST_F(ShapeRendererTest, TransparentFillDrawsOnlyOutline)
{
	const auto image{ render({
		ax::Rectangle{ .position = { 0, 0 }, .size = { 128, 128 }, .fill = s_red, .screenSpace = true },
		ax::Rectangle{ .position = { 32, 32 }, .size = { 64, 64 }, .fill = s_transparent,
			.outline = { .colour = s_blue, .thickness = 4 }, .z = 1, .screenSpace = true } }) };

	EXPECT_PIXEL(image, 33, 64, s_blue);
	EXPECT_PIXEL(image, 64, 64, s_red);
	EXPECT_PIXEL(image, 10, 10, s_red);
}

//
// Coordinate spaces
//

TEST_F(ShapeRendererTest, WorldSpaceShapesFollowCamera)
{
	const ax::Rectangle world{ .position = { -10, -10 }, .size = { 20, 20 }, .fill = s_red };
	const ax::Rectangle ui{ .position = { 0, 0 }, .size = { 8, 8 }, .fill = s_green, .screenSpace = true };

	auto view{ screen_view() };
	const auto centred{ render({ world, ui }, view) };
	EXPECT_PIXEL(centred, 64, 64, s_red);
	EXPECT_PIXEL(centred, 56, 64, s_red);
	EXPECT_PIXEL(centred, 76, 64, s_black);
	EXPECT_PIXEL(centred, 2, 2, s_green);

	view.cameraPosition = { 30, 0 };
	const auto panned{ render({ world, ui }, view) };
	EXPECT_PIXEL(panned, 34, 64, s_red);
	EXPECT_PIXEL(panned, 64, 64, s_black);
	EXPECT_PIXEL(panned, 2, 2, s_green);

	view.cameraPosition = { 0, 0 };
	view.zoom = 2.0f;
	const auto zoomed{ render({ world, ui }, view) };
	EXPECT_PIXEL(zoomed, 64 + 17, 64, s_red);
	EXPECT_PIXEL(zoomed, 64 + 22, 64, s_black);
	EXPECT_PIXEL(zoomed, 2, 2, s_green);
	EXPECT_PIXEL(zoomed, 10, 10, s_black);
}

//
// Batching, consistency and robustness
//

TEST_F(ShapeRendererTest, EachDrawCallIsBatched)
{
	ax::ShapeList opaque;
	for (int i = 0; i < 100; ++i)
		opaque.push_back(ax::Circle{ .position = { static_cast<float>(i), 64 }, .radius = 4, .fill = s_red, .screenSpace = true });

	render(opaque);
	EXPECT_EQ(ax::ShapeRenderer::last_draw_stats().drawCalls, 1u);

	auto mixed{ opaque };
	for (int i = 0; i < 100; ++i)
		mixed.push_back(ax::Rectangle{ .position = { static_cast<float>(i), 10 }, .size = { 4, 4 }, .fill = { 0, 1, 0, 0.5f }, .screenSpace = true });
	render(mixed);
	EXPECT_EQ(ax::ShapeRenderer::last_draw_stats().drawCalls, 2u);

	render(ax::ShapeList{});
	EXPECT_EQ(ax::ShapeRenderer::last_draw_stats().drawCalls, 0u);
}

TEST_F(ShapeRendererTest, MultipleDrawCallsInOnePassAreAllRendered)
{
	std::vector<ax::ShapeList> calls;
	for (int i = 0; i < 8; ++i)
		calls.push_back({ ax::Rectangle{ .position = { 16.0f * static_cast<float>(i), 0 }, .size = { 16, 16 }, .fill = i % 2 ? s_red : s_green, .screenSpace = true } });

	const auto image{ render(calls) };
	for (int i = 0; i < 8; ++i)
		EXPECT_PIXEL(image, 16.0f * static_cast<float>(i) + 8.0f, 8.0f, i % 2 ? s_red : s_green) << "draw call " << i;
	EXPECT_PIXEL(image, 64, 32, s_black);
}

TEST_F(ShapeRendererTest, LargeBatchesAreSplitToFitBufferLimits)
{
	ax::ShapeList shapes;
	shapes.push_back(ax::Rectangle{ .position = { 0, 0 }, .size = { 16, 16 }, .fill = s_red, .screenSpace = true });
	for (int i = 0; i < 20000; ++i)
		shapes.push_back(ax::Circle{ .position = { 64, 64 }, .radius = 4, .fill = s_blue, .screenSpace = true, .segments = 128 });
	shapes.push_back(ax::Rectangle{ .position = { 112, 112 }, .size = { 16, 16 }, .fill = s_green, .screenSpace = true });

	const auto image{ render(shapes) };
	EXPECT_GT(ax::ShapeRenderer::last_draw_stats().drawCalls, 1u);
	EXPECT_PIXEL(image, 8, 8, s_red);
	EXPECT_PIXEL(image, 64, 64, s_blue);
	EXPECT_PIXEL(image, 120, 120, s_green);
}

TEST_F(ShapeRendererTest, RenderingIsDeterministic)
{
	const ax::ShapeList scene{
		ax::Rectangle{ .position = { 10, 10 }, .size = { 50, 30 }, .cornerRadius = 6, .fill = s_red, .outline = { s_blue, 2 }, .rotation = 0.3f, .z = 1, .screenSpace = true },
		ax::Circle{ .position = { 80, 40 }, .radius = 25, .fill = { 0, 1, 0, 0.6f }, .outline = { s_red, 3 }, .z = 2, .screenSpace = true },
		ax::Ellipse{ .position = { 40, 90 }, .radii = { 30, 12 }, .fill = s_blue, .rotation = -0.7f, .screenSpace = true },
		ax::Triangle{ .position = { 90, 90 }, .points = { { { -20, 20 }, { 20, 20 }, { 0, -20 } } }, .fill = { 1, 1, 0, 1 }, .outline = { s_black, 2 }, .rotation = 1.0f, .z = 3, .screenSpace = true },
		ax::Line{ .start = { 5, 120 }, .end = { 120, 5 }, .thickness = 5, .cap = ax::LineCap::Round, .fill = { 1, 1, 1, 0.8f }, .z = 4, .screenSpace = true },
		ax::Polygon{ .position = { 64, 64 }, .points = { { 0, -15 }, { 5, -5 }, { 15, 0 }, { 5, 5 }, { 0, 15 }, { -5, 5 }, { -15, 0 }, { -5, -5 } }, .fill = { 1, 0, 1, 1 }, .z = 5, .screenSpace = true },
	};

	const auto first{ render(scene) };
	EXPECT_EQ(render(scene), first);
	EXPECT_EQ(render(scene), first);

	// Distinct, opaque z values make the result independent of how shapes are split across calls.
	ax::ShapeList opaque;
	for (int i = 0; i < 6; ++i)
		opaque.push_back(ax::Circle{ .position = { 20.0f + 15.0f * static_cast<float>(i), 64 }, .radius = 18, .fill = i % 2 ? s_red : s_blue, .z = static_cast<float>(i % 3), .screenSpace = true });
	const auto batched{ render(opaque) };
	std::vector<ax::ShapeList> separate;
	for (const auto& shape : opaque)
		separate.push_back({ shape });
	EXPECT_EQ(render(separate), batched);
}

TEST_F(ShapeRendererTest, DegenerateShapesDrawNothing)
{
	const auto image{ render({
		ax::Rectangle{ .position = { 10, 10 }, .size = { 0, 20 }, .fill = s_red, .screenSpace = true },
		ax::Circle{ .position = { 30, 30 }, .radius = 0, .fill = s_red, .screenSpace = true },
		ax::Line{ .start = { 50, 50 }, .end = { 50, 50 }, .thickness = 4, .fill = s_red, .screenSpace = true },
		ax::Line{ .start = { 50, 50 }, .end = { 80, 50 }, .thickness = 0, .fill = s_red, .screenSpace = true },
		ax::Polygon{ .position = { 60, 60 }, .points = { { 0, 0 }, { 10, 10 } }, .fill = s_red, .screenSpace = true },
		ax::Polygon{ .position = { 60, 60 }, .points = { { 0, 0 }, { 10, 10 }, { 20, 20 } }, .fill = s_red, .screenSpace = true },
		ax::Triangle{ .position = { 60, 60 }, .fill = s_red, .screenSpace = true },
		ax::Rectangle{ .position = { 10, 10 }, .size = { 20, 20 }, .fill = s_transparent, .screenSpace = true },
	}) };

	EXPECT_EQ(ax::ShapeRenderer::last_draw_stats().indices, 0u);
	EXPECT_TRUE(std::all_of(image.rgba.begin(), image.rgba.end(), [i = 0](std::uint8_t v) mutable { return (i++ % 4 == 3) ? v == 255 : v == 0; }));
}

TEST_F(ShapeRendererTest, DrawWithoutViewLogsError)
{
	testlog::LogCapture::instance().expect_total_error_count(1);

	auto encoder{ s_device.CreateCommandEncoder() };
	wgpu::RenderPassDescriptor passDesc{};
	wgpu::TextureDescriptor desc{};
	desc.size = { 4, 4 };
	desc.format = s_config.colourFormat;
	desc.sampleCount = s_config.sampleCount;
	desc.usage = wgpu::TextureUsage::RenderAttachment;
	const auto texture{ s_device.CreateTexture(&desc) };
	wgpu::RenderPassColorAttachment colour{};
	colour.view = texture.CreateView();
	colour.loadOp = wgpu::LoadOp::Clear;
	colour.storeOp = wgpu::StoreOp::Store;
	passDesc.colorAttachmentCount = 1;
	passDesc.colorAttachments = &colour;
	auto pass{ encoder.BeginRenderPass(&passDesc) };
	ax::ShapeRenderer::draw(pass, { ax::Circle{ .radius = 4 } });
	pass.End();

	EXPECT_EQ(ax::ShapeRenderer::last_draw_stats().drawCalls, 0u);
	EXPECT_LOG_CONTAINS("begin_frame");
}

//
// Lua bindings
//

namespace
{
	sol::state make_lua_state()
	{
		sol::state state;
		state.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table);
		ax::lua::bindings::setup_vector_bindings(state);
		state["shapes"] = ax::lua::bindings::create_shape_table(state);
		return state;
	}

	sol::protected_function load_lua(sol::state& state, const char* code)
	{
		auto result{ state.safe_script(code, sol::script_pass_on_error) };
		if (!result.valid())
		{
			const sol::error error = result;
			ADD_FAILURE() << error.what();
			return {};
		}
		return result.get<sol::protected_function>();
	}

	void call_lua(const sol::protected_function& fn, wgpu::RenderPassEncoder& pass)
	{
		// Mirrors how app.window.on_render passes the pass to Lua.
		const auto result{ fn(pass) };
		if (!result.valid())
		{
			const sol::error error = result;
			ADD_FAILURE() << error.what();
		}
	}
}

TEST_F(ShapeRendererTest, LuaShapeListMatchesNativeShapes)
{
	const ax::ShapeList native{
		ax::Rectangle{ .position = { 8, 8 }, .size = { 48, 32 }, .cornerRadius = 6, .fill = s_red,
			.outline = { .colour = s_blue, .thickness = 3 }, .rotation = 0.3f, .z = 1, .screenSpace = true },
		ax::Circle{ .position = { 90, 30 }, .radius = 20, .fill = s_green, .z = 2, .screenSpace = true },
		ax::Ellipse{ .position = { 30, 90 }, .radii = { 24, 12 }, .fill = { 1, 1, 0, 0.5f }, .rotation = -0.4f, .z = 3, .screenSpace = true },
		ax::Triangle{ .position = { 90, 90 }, .points = { glm::vec2{ 0, -20 }, { 20, 20 }, { -20, 20 } }, .scale = { 0.8f, 0.8f },
			.fill = s_blue, .outline = { .colour = s_red, .thickness = 2 }, .z = 4, .screenSpace = true },
		ax::Polygon{ .position = { 64, 64 }, .points = { { -10, -10 }, { 10, -10 }, { 0, 0 }, { 10, 10 }, { -10, 10 } },
			.fill = { 1, 0, 1, 1 }, .z = 5, .screenSpace = true },
		ax::Line{ .start = { 10, 120 }, .end = { 118, 70 }, .thickness = 6, .cap = ax::LineCap::Round,
			.fill = { 0, 1, 1, 1 }, .z = 6, .screenSpace = true },
	};

	auto state{ make_lua_state() };
	const auto draw{ load_lua(state, R"(
		local list = shapes.list()
		list:add({ type = "rectangle", position = vec2:new(8, 8), size = { 48, 32 }, corner_radius = 6, fill = { 1, 0, 0 },
			outline = { colour = vec4:new(0, 0, 1, 1), thickness = 3 }, rotation = 0.3, z = 1, screen_space = true })
		list:add({ type = "circle", x = 90, y = 30, radius = 20, fill = { r = 0, g = 1, b = 0 }, z = 2, screen_space = true })
		list:add({ type = "ellipse", position = { x = 30, y = 90 }, rx = 24, ry = 12, fill = { 1, 1, 0, 0.5 }, rotation = -0.4, z = 3, screen_space = true })
		list:add({ type = "triangle", position = { 90, 90 }, points = { { 0, -20 }, { 20, 20 }, { -20, 20 } }, scale = 0.8,
			fill = { 0, 0, 1 }, outline = { color = { 1, 0, 0 }, thickness = 2 }, z = 4, screen_space = true })
		list:add({ type = "polygon", position = { 64, 64 }, points = { { -10, -10 }, { 10, -10 }, { 0, 0 }, { 10, 10 }, { -10, 10 } },
			fill = { 1, 0, 1 }, z = 5, screen_space = true })
		list:add({ type = "line", start = { 10, 120 }, finish = { 118, 70 }, thickness = 6, cap = shapes.cap.round,
			fill = { 0, 1, 1 }, z = 6, screen_space = true })
		assert(#list == 6)
		return function(pass) shapes.draw(pass, list) end
	)") };
	ASSERT_TRUE(draw.valid());

	const auto expected{ render(native) };
	const auto actual{ render_pass([&draw](wgpu::RenderPassEncoder& pass) { call_lua(draw, pass); }) };
	EXPECT_EQ(actual, expected);
	EXPECT_EQ(ax::ShapeRenderer::last_draw_stats().drawCalls, 2u);
}

TEST_F(ShapeRendererTest, LuaImmediateTablesAreBatchedPerCall)
{
	auto state{ make_lua_state() };
	const auto draw{ load_lua(state, R"(
		return function(pass)
			shapes.draw(pass, {
				{ type = "rect", x = 32, y = 32, width = 64, height = 32, fill = { 1, 0, 0 }, screen_space = true },
				{ type = "circle", position = { 64, 96 }, radius = 10, fill = { 0, 1, 0 }, z = 1, screen_space = true },
			})
			local stats = shapes.stats()
			assert(stats.draw_calls == 1 and stats.vertices > 0 and stats.indices > 0)
			shapes.draw(pass, { type = "line", x1 = 0, y1 = 10, x2 = 127, y2 = 10, thickness = 4, fill = { 0, 0, 1 }, screen_space = true })
		end
	)") };
	ASSERT_TRUE(draw.valid());

	const auto image{ render_pass([&draw](wgpu::RenderPassEncoder& pass) { call_lua(draw, pass); }) };
	EXPECT_PIXEL(image, 64, 48, s_red);
	EXPECT_PIXEL(image, 64, 96, s_green);
	EXPECT_PIXEL(image, 64, 10, s_blue);
	EXPECT_PIXEL(image, 64, 20, s_black);
}

TEST_F(ShapeRendererTest, LuaShapeListSettersUpdateShapes)
{
	auto state{ make_lua_state() };
	const auto draw{ load_lua(state, R"(
		local list = shapes.list()
		local rect = list:add({ type = "rectangle", x = 0, y = 0, width = 20, height = 20, fill = { 1, 0, 0 }, screen_space = true })
		local line = list:add({ type = "line", start = { 0, 0 }, ["end"] = { 20, 0 }, thickness = 4, fill = { 1, 1, 1 }, screen_space = true })
		local gone = list:add({ type = "circle", x = 100, y = 100, radius = 10, fill = { 1, 1, 1 }, screen_space = true })

		list:set_position(rect, 54, 54)
		list:set_fill(rect, 0, 0, 1)
		list:set_outline(rect, 2, 0, 1, 0)
		list:set_z(rect, 1)
		list:set_position(line, 10, 110)
		local x, y = list:get_position(line)
		assert(x == 10 and y == 110)
		list:remove(gone)
		assert(list:size() == 2)
		return function(pass) shapes.draw(pass, list) end
	)") };
	ASSERT_TRUE(draw.valid());

	const auto image{ render_pass([&draw](wgpu::RenderPassEncoder& pass) { call_lua(draw, pass); }) };
	EXPECT_PIXEL(image, 64, 64, s_blue);
	EXPECT_PIXEL(image, 55, 64, s_green);
	EXPECT_PIXEL(image, 20, 110, glm::vec4(1.0f));
	EXPECT_PIXEL(image, 20, 120, s_black);
	EXPECT_PIXEL(image, 100, 100, s_black);
}

TEST_F(ShapeRendererTest, LuaDebugShapesDrawOnFlush)
{
	ax::DebugShapes::clear();
	auto state{ make_lua_state() };
	const auto draw{ load_lua(state, R"(
		return function(pass)
			pass.debug_rect_fill(16, 16, 32, 32, { 1, 0, 0 })
			pass.debug_rect_outline(64, 16, 48, 48, { 0, 1, 0 }, 4)
			pass:debug_line(0, 100, 127, 100, { 0, 0, 1 }, 4)
		end
	)") };
	ASSERT_TRUE(draw.valid());

	const auto image{ render_pass([&draw](wgpu::RenderPassEncoder& pass)
		{
			call_lua(draw, pass);
			// Debug shapes go on top of shapes drawn with a lower z.
			ax::ShapeRenderer::draw(pass, { ax::Rectangle{ .position = { 0, 0 }, .size = { 24, 24 }, .fill = s_blue, .z = 10 } });
			ax::DebugShapes::flush(pass);
		}, { .viewport = glm::vec2{ s_size }, .cameraPosition = glm::vec2{ s_size * 0.5f } }) };
	EXPECT_TRUE(ax::DebugShapes::queued().empty());
	EXPECT_EQ(ax::ShapeRenderer::last_draw_stats().drawCalls, 1u);

	EXPECT_PIXEL(image, 32, 32, s_red);
	EXPECT_PIXEL(image, 20, 20, s_red);
	EXPECT_PIXEL(image, 8, 8, s_blue);
	EXPECT_PIXEL(image, 66, 40, s_green);
	EXPECT_PIXEL(image, 88, 40, s_black);
	EXPECT_PIXEL(image, 64, 100, s_blue);
	EXPECT_PIXEL(image, 64, 90, s_black);
}