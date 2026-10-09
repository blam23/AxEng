#include <gtest/gtest.h>
#include <dawn/native/DawnNative.h>
#include <windows.h>

#include "axeng/core/texture.h"

#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	constexpr unsigned char s_shader[]{
		#embed "../../../shaders/basic_shader.wgsl"
	};
	constexpr std::uint32_t s_size{ 64 };
	using Pixel = std::array<std::uint8_t, 4>;
}

class SpriteShaderTest : public testing::Test
{
protected:
	static void SetUpTestSuite()
	{
		char systemDirectory[MAX_PATH]{};
		const auto length{ GetSystemDirectoryA(systemDirectory, MAX_PATH) };
		ASSERT_GT(length, 0u);
		ASSERT_LT(length, static_cast<UINT>(MAX_PATH));
		const std::string systemPath{ std::string(systemDirectory, length) + '\\' };
		const char* searchPaths[]{ systemPath.c_str() };
		dawn::native::DawnInstanceDescriptor dawnDesc{};
		dawnDesc.additionalRuntimeSearchPathsCount = 1;
		dawnDesc.additionalRuntimeSearchPaths = searchPaths;
		constexpr auto timedWait{ wgpu::InstanceFeatureName::TimedWaitAny };
		wgpu::InstanceDescriptor instanceDesc{};
		instanceDesc.nextInChain = &dawnDesc;
		instanceDesc.requiredFeatureCount = 1;
		instanceDesc.requiredFeatures = &timedWait;
		s_instance = wgpu::CreateInstance(&instanceDesc);
		ASSERT_NE(s_instance, nullptr);

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

		wgpu::DeviceDescriptor deviceDesc{};
		deviceDesc.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message)
			{
				ADD_FAILURE() << std::string_view(message.data, message.length);
			});
		s_instance.WaitAny(adapter.RequestDevice(&deviceDesc, wgpu::CallbackMode::WaitAnyOnly,
			[](wgpu::RequestDeviceStatus status, wgpu::Device result, wgpu::StringView message)
			{
				EXPECT_EQ(status, wgpu::RequestDeviceStatus::Success) << std::string_view(message.data, message.length);
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

	static std::vector<Pixel> render(const std::vector<Pixel>& pixels, std::uint32_t width,
		std::uint32_t height, const std::vector<ax::SpriteGpuData>& sprites, const char* entry = "fs_main")
	{
		wgpu::ShaderSourceWGSL source{};
		source.code = { reinterpret_cast<const char*>(s_shader), sizeof(s_shader) };
		wgpu::ShaderModuleDescriptor shaderDesc{};
		shaderDesc.nextInChain = &source;
		const auto shader{ s_device.CreateShaderModule(&shaderDesc) };
		std::array<wgpu::BindGroupLayoutEntry, 3> spriteLayoutEntries{};
		spriteLayoutEntries[0].binding = 0;
		spriteLayoutEntries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
		spriteLayoutEntries[0].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
		spriteLayoutEntries[1].binding = 1;
		spriteLayoutEntries[1].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
		spriteLayoutEntries[1].texture.sampleType = wgpu::TextureSampleType::Float;
		spriteLayoutEntries[2].binding = 2;
		spriteLayoutEntries[2].visibility = wgpu::ShaderStage::Fragment;
		spriteLayoutEntries[2].sampler.type = wgpu::SamplerBindingType::Filtering;
		wgpu::BindGroupLayoutDescriptor layoutDesc{};
		layoutDesc.entryCount = spriteLayoutEntries.size();
		layoutDesc.entries = spriteLayoutEntries.data();
		const auto spriteLayout{ s_device.CreateBindGroupLayout(&layoutDesc) };
		wgpu::BindGroupLayoutEntry cameraLayoutEntry{};
		cameraLayoutEntry.binding = 0;
		cameraLayoutEntry.visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
		cameraLayoutEntry.buffer.type = wgpu::BufferBindingType::Uniform;
		layoutDesc.entryCount = 1;
		layoutDesc.entries = &cameraLayoutEntry;
		const auto cameraLayout{ s_device.CreateBindGroupLayout(&layoutDesc) };
		const std::array<wgpu::BindGroupLayout, 2> layouts{ spriteLayout, cameraLayout };
		wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{};
		pipelineLayoutDesc.bindGroupLayoutCount = layouts.size();
		pipelineLayoutDesc.bindGroupLayouts = layouts.data();
		wgpu::ColorTargetState colourTarget{};
		colourTarget.format = wgpu::TextureFormat::RGBA8Unorm;
		wgpu::FragmentState fragment{};
		fragment.module = shader;
		fragment.entryPoint = entry;
		fragment.targetCount = 1;
		fragment.targets = &colourTarget;
		wgpu::RenderPipelineDescriptor pipelineDesc{};
		pipelineDesc.layout = s_device.CreatePipelineLayout(&pipelineLayoutDesc);
		pipelineDesc.vertex.module = shader;
		pipelineDesc.vertex.entryPoint = "vs_main";
		pipelineDesc.fragment = &fragment;
		const auto pipeline{ s_device.CreateRenderPipeline(&pipelineDesc) };
		std::array<wgpu::ColorTargetState, 2> oitTargets{};
		oitTargets[0].format = wgpu::TextureFormat::RGBA16Float;
		oitTargets[1].format = wgpu::TextureFormat::R8Unorm;
		fragment.entryPoint = "fs_oit";
		fragment.targetCount = oitTargets.size();
		fragment.targets = oitTargets.data();
		EXPECT_NE(s_device.CreateRenderPipeline(&pipelineDesc), nullptr);

		wgpu::TextureDescriptor textureDesc{};
		textureDesc.size = { width, height };
		textureDesc.format = wgpu::TextureFormat::RGBA8Unorm;
		textureDesc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
		const auto texture{ s_device.CreateTexture(&textureDesc) };
		wgpu::TexelCopyTextureInfo upload{};
		upload.texture = texture;
		wgpu::TexelCopyBufferLayout uploadLayout{};
		uploadLayout.bytesPerRow = width * 4;
		uploadLayout.rowsPerImage = height;
		s_device.GetQueue().WriteTexture(&upload, pixels.data(), pixels.size() * sizeof(Pixel),
			&uploadLayout, &textureDesc.size);

		wgpu::BufferDescriptor bufferDesc{};
		bufferDesc.size = sprites.size() * sizeof(ax::SpriteGpuData);
		bufferDesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
		const auto spriteBuffer{ s_device.CreateBuffer(&bufferDesc) };
		s_device.GetQueue().WriteBuffer(spriteBuffer, 0, sprites.data(), bufferDesc.size);
		const std::array<float, 8> camera{ s_size, s_size, 0, 0, 0, 0, 1, 0 };
		bufferDesc.size = sizeof(camera);
		bufferDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
		const auto cameraBuffer{ s_device.CreateBuffer(&bufferDesc) };
		s_device.GetQueue().WriteBuffer(cameraBuffer, 0, camera.data(), sizeof(camera));

		wgpu::SamplerDescriptor samplerDesc{};
		samplerDesc.magFilter = wgpu::FilterMode::Linear;
		samplerDesc.minFilter = wgpu::FilterMode::Linear;
		std::array<wgpu::BindGroupEntry, 3> entries{};
		entries[0].binding = 0;
		entries[0].buffer = spriteBuffer;
		entries[0].size = spriteBuffer.GetSize();
		entries[1].binding = 1;
		entries[1].textureView = texture.CreateView();
		entries[2].binding = 2;
		entries[2].sampler = s_device.CreateSampler(&samplerDesc);
		wgpu::BindGroupDescriptor groupDesc{};
		groupDesc.layout = pipeline.GetBindGroupLayout(0);
		groupDesc.entryCount = entries.size();
		groupDesc.entries = entries.data();
		const auto spriteGroup{ s_device.CreateBindGroup(&groupDesc) };
		wgpu::BindGroupEntry cameraEntry{};
		cameraEntry.binding = 0;
		cameraEntry.buffer = cameraBuffer;
		cameraEntry.size = sizeof(camera);
		groupDesc.layout = pipeline.GetBindGroupLayout(1);
		groupDesc.entryCount = 1;
		groupDesc.entries = &cameraEntry;
		const auto cameraGroup{ s_device.CreateBindGroup(&groupDesc) };

		textureDesc.size = { s_size, s_size };
		textureDesc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
		const auto output{ s_device.CreateTexture(&textureDesc) };
		bufferDesc.size = s_size * s_size * sizeof(Pixel);
		bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
		const auto readback{ s_device.CreateBuffer(&bufferDesc) };
		const auto encoder{ s_device.CreateCommandEncoder() };
		wgpu::RenderPassColorAttachment attachment{};
		attachment.view = output.CreateView();
		attachment.loadOp = wgpu::LoadOp::Clear;
		attachment.storeOp = wgpu::StoreOp::Store;
		wgpu::RenderPassDescriptor passDesc{};
		passDesc.colorAttachmentCount = 1;
		passDesc.colorAttachments = &attachment;
		const auto pass{ encoder.BeginRenderPass(&passDesc) };
		pass.SetPipeline(pipeline);
		pass.SetBindGroup(0, spriteGroup);
		pass.SetBindGroup(1, cameraGroup);
		pass.Draw(6, static_cast<std::uint32_t>(sprites.size()));
		pass.End();

		wgpu::TexelCopyTextureInfo copySource{};
		copySource.texture = output;
		wgpu::TexelCopyBufferInfo destination{};
		destination.buffer = readback;
		destination.layout.bytesPerRow = s_size * sizeof(Pixel);
		destination.layout.rowsPerImage = s_size;
		encoder.CopyTextureToBuffer(&copySource, &destination, &textureDesc.size);
		const auto commands{ encoder.Finish() };
		s_device.GetQueue().Submit(1, &commands);
		bool mapped{ false };
		s_instance.WaitAny(readback.MapAsync(wgpu::MapMode::Read, 0, bufferDesc.size, wgpu::CallbackMode::WaitAnyOnly,
			[&mapped](wgpu::MapAsyncStatus status, wgpu::StringView message)
			{
				EXPECT_EQ(status, wgpu::MapAsyncStatus::Success) << std::string_view(message.data, message.length);
				mapped = status == wgpu::MapAsyncStatus::Success;
			}), UINT64_MAX);
		if (!mapped)
			return {};
		std::vector<Pixel> result(s_size * s_size);
		std::memcpy(result.data(), readback.GetConstMappedRange(), bufferDesc.size);
		readback.Unmap();
		return result;
	}

	static inline wgpu::Instance s_instance{};
	static inline wgpu::Device s_device{};
};

TEST_F(SpriteShaderTest, AtlasTileEdgesStayOpaqueAtFractionalScale)
{
	const std::vector<Pixel> atlas{
		{ 0, 0, 255, 255 }, { 255, 0, 0, 255 }, { 0, 255, 0, 0 },
		{ 0, 0, 255, 255 }, { 255, 0, 0, 255 }, { 0, 255, 0, 0 },
		{ 0, 0, 255, 255 }, { 0, 0, 255, 255 }, { 0, 255, 0, 0 }
	};
	ax::SpriteGpuData tile{};
	tile.pos = { 8.25f, 8.25f };
	tile.scale = { 17.5f, 17.5f };
	tile.region = { 1, 0, 1, 2 };
	tile.useRegion = 1;
	tile.screenSpace = 1;
	auto neighbour{ tile };
	neighbour.pos.x += 17.5f;
	const auto image{ render(atlas, 3, 3, { tile, neighbour }, "fs_opaque") };
	ASSERT_EQ(image.size(), s_size * s_size);
	for (std::uint32_t y{ 9 }; y < 43; ++y)
		for (std::uint32_t x{ 9 }; x < 43; ++x)
			EXPECT_EQ(image[y * s_size + x], (Pixel{ 255, 0, 0, 255 })) << x << ", " << y;
}

TEST_F(SpriteShaderTest, TransparentTexelRgbDoesNotCreateFringes)
{
	ax::SpriteGpuData sprite{};
	sprite.pos = { 8.25f, 8.25f };
	sprite.scale = { 16, 16 };
	sprite.rotation = 0.3f;
	sprite.screenSpace = 1;
	const auto black{ render({ { 255, 255, 255, 255 }, { 0, 0, 0, 0 } }, 2, 1, { sprite }) };
	const auto green{ render({ { 255, 255, 255, 255 }, { 0, 255, 0, 0 } }, 2, 1, { sprite }) };
	ASSERT_EQ(black.size(), s_size * s_size);
	ASSERT_EQ(green.size(), black.size());
	EXPECT_EQ(black, green);
	bool foundTransition{ false };
	for (const auto& pixel : black)
	{
		if (pixel[3] > 0 && pixel[3] < 255)
		{
			foundTransition = true;
			EXPECT_EQ(pixel[0], 255);
			EXPECT_EQ(pixel[1], 255);
			EXPECT_EQ(pixel[2], 255);
		}
	}
	EXPECT_TRUE(foundTransition);
}

TEST_F(SpriteShaderTest, RotatedScaledSpritePreservesColourAndTint)
{
	ax::SpriteGpuData sprite{};
	sprite.pos = { 16, 16 };
	sprite.scale = { 16, 16 };
	sprite.rotation = 0.4f;
	sprite.screenSpace = 1;
	sprite.tint = { 0.5f, 0.25f, 1, 0.5f };
	const auto image{ render(std::vector<Pixel>(4, { 255, 255, 255, 255 }), 2, 2, { sprite }) };
	ASSERT_EQ(image.size(), s_size * s_size);
	const Pixel expected{ 128, 64, 255, 128 };
	for (std::size_t i{ 0 }; i < expected.size(); ++i)
		EXPECT_NEAR(image[32 * s_size + 32][i], expected[i], 1);
	EXPECT_EQ(image[0], (Pixel{}));
}
