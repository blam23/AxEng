#include <gtest/gtest.h>
#include <dawn/native/DawnNative.h>
#include <windows.h>

#include "axeng/core/font_data.h"
#include "axeng/core/msdf_font.h"
#include "axeng/core/sdf_font.h"
#include "axeng/core/texture.h"
#include "axeng/core/retained_sprites.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	constexpr unsigned char s_shader[]{
		#embed "../../../shaders/basic_shader.wgsl"
	};
	constexpr unsigned char s_sdfShader[]{
		#embed "../../../shaders/sdf_shader.wgsl"
	};
	constexpr std::uint32_t s_size{ 64 };
	using Pixel = std::array<std::uint8_t, 4>;

	std::vector<ax::SdfSpriteGpuData> make_sdf_text(const ax::SdfFont& font, std::string_view text,
		float centerX, float baselineY, float textScale, glm::vec4 tint,
		bool msdf = false)
	{
		float textWidth{};
		for (const unsigned char character : text)
		{
			const auto index{ static_cast<std::size_t>(character - ax::font_first_codepoint) };
			if (index < font.glyphs.size())
				textWidth += font.glyphs[index].advance * textScale;
		}

		float penX{ centerX - textWidth * 0.5f };
		std::vector<ax::SdfSpriteGpuData> sprites{};
		sprites.reserve(text.size());
		for (const unsigned char character : text)
		{
			const auto index{ static_cast<std::size_t>(character - ax::font_first_codepoint) };
			if (index >= font.glyphs.size())
				continue;

			const auto& glyph{ font.glyphs[index] };
			if (glyph.width > 0 && glyph.height > 0)
			{
				ax::SpriteData sprite{};
				sprite.pos = { penX + glyph.xOffset * textScale, baselineY + glyph.yOffset * textScale };
				sprite.scale = { font.texelScale * textScale, font.texelScale * textScale };
				sprite.tint = tint;
				sprite.region = { static_cast<float>(glyph.x), static_cast<float>(glyph.y),
					static_cast<float>(glyph.width), static_cast<float>(glyph.height) };
				sprite.useRegion = ax::sprite_flag_region;
				sprite.screenSpace = 1;
				sprites.push_back(ax::pack_sdf_sprite(sprite, {}, msdf));
			}
			penX += glyph.advance * textScale;
		}
		return sprites;
	}

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
		std::uint32_t height, const std::vector<ax::SpriteData>& input, const char* entry = "fs_main",
		wgpu::Buffer retained = nullptr, std::uint32_t outputWidth = s_size,
		std::uint32_t outputHeight = s_size, std::array<double, 4> clearColour = { 0.0, 0.0, 0.0, 0.0 },
		bool blend = false)
	{
		std::vector<ax::SpriteGpuData> sprites{};
		for (const auto& sprite : input)
			sprites.push_back(ax::pack_sprite(sprite));
		return render_records({ reinterpret_cast<const char*>(s_shader), sizeof(s_shader) }, pixels, width, height,
			sprites.data(), sprites.size(), sizeof(ax::SpriteGpuData), entry, retained, outputWidth, outputHeight,
			clearColour, blend);
	}

	// Distance-field sprites use their own shader and record layout.
	static std::vector<Pixel> render_sdf(const std::vector<Pixel>& pixels, std::uint32_t width,
		std::uint32_t height, const std::vector<ax::SdfSpriteGpuData>& sprites, const char* entry = "fs_main",
		std::uint32_t outputWidth = s_size, std::uint32_t outputHeight = s_size,
		std::array<double, 4> clearColour = { 0.0, 0.0, 0.0, 0.0 }, bool blend = false)
	{
		return render_records({ reinterpret_cast<const char*>(s_sdfShader), sizeof(s_sdfShader) }, pixels, width,
			height, sprites.data(), sprites.size(), sizeof(ax::SdfSpriteGpuData), entry, nullptr, outputWidth,
			outputHeight, clearColour, blend);
	}

	static std::vector<Pixel> render_records(wgpu::StringView shaderCode, const std::vector<Pixel>& pixels,
		std::uint32_t width, std::uint32_t height, const void* records, std::size_t recordCount,
		std::size_t recordSize, const char* entry, wgpu::Buffer retained, std::uint32_t outputWidth,
		std::uint32_t outputHeight, std::array<double, 4> clearColour, bool blend)
	{
		wgpu::ShaderSourceWGSL source{};
		source.code = shaderCode;
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
		wgpu::BlendState blendState{};
		blendState.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
		blendState.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
		blendState.color.operation = wgpu::BlendOperation::Add;
		blendState.alpha.srcFactor = wgpu::BlendFactor::One;
		blendState.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
		blendState.alpha.operation = wgpu::BlendOperation::Add;
		wgpu::ColorTargetState colourTarget{};
		colourTarget.format = wgpu::TextureFormat::RGBA8Unorm;
		colourTarget.blend = blend ? &blendState : nullptr;
		wgpu::FragmentState fragment{};
		fragment.module = shader;
		fragment.entryPoint = entry;
		fragment.targetCount = 1;
		fragment.targets = &colourTarget;
		wgpu::RenderPipelineDescriptor pipelineDesc{};
		pipelineDesc.layout = s_device.CreatePipelineLayout(&pipelineLayoutDesc);
		pipelineDesc.vertex.module = shader;
		pipelineDesc.vertex.entryPoint = "vs_main";
		// The tests position sprites in screen pixels.
		wgpu::ConstantEntry screenSpace{};
		screenSpace.key = "screen_space";
		screenSpace.value = 1.0;
		pipelineDesc.vertex.constantCount = 1;
		pipelineDesc.vertex.constants = &screenSpace;
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
		bufferDesc.size = recordCount * recordSize;
		bufferDesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
		const auto spriteBuffer{ retained ? retained : s_device.CreateBuffer(&bufferDesc) };
		if (!retained)
			s_device.GetQueue().WriteBuffer(spriteBuffer, 0, records, bufferDesc.size);
		const std::array<float, 8> camera{ static_cast<float>(outputWidth), static_cast<float>(outputHeight),
			0, 0, 0, 0, 1, 0 };
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

		textureDesc.size = { outputWidth, outputHeight };
		textureDesc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
		const auto output{ s_device.CreateTexture(&textureDesc) };
		const std::uint32_t bytesPerRow{
			(outputWidth * static_cast<std::uint32_t>(sizeof(Pixel)) + 255u) & ~255u };
		bufferDesc.size = static_cast<std::uint64_t>(bytesPerRow) * outputHeight;
		bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
		const auto readback{ s_device.CreateBuffer(&bufferDesc) };
		const auto encoder{ s_device.CreateCommandEncoder() };
		wgpu::RenderPassColorAttachment attachment{};
		attachment.view = output.CreateView();
		attachment.loadOp = wgpu::LoadOp::Clear;
		attachment.storeOp = wgpu::StoreOp::Store;
		attachment.clearValue = { clearColour[0], clearColour[1], clearColour[2], clearColour[3] };
		wgpu::RenderPassDescriptor passDesc{};
		passDesc.colorAttachmentCount = 1;
		passDesc.colorAttachments = &attachment;
		const auto pass{ encoder.BeginRenderPass(&passDesc) };
		pass.SetPipeline(pipeline);
		pass.SetBindGroup(0, spriteGroup);
		pass.SetBindGroup(1, cameraGroup);
		pass.Draw(6, static_cast<std::uint32_t>(recordCount));
		pass.End();

		wgpu::TexelCopyTextureInfo copySource{};
		copySource.texture = output;
		wgpu::TexelCopyBufferInfo destination{};
		destination.buffer = readback;
		destination.layout.bytesPerRow = bytesPerRow;
		destination.layout.rowsPerImage = outputHeight;
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
		std::vector<Pixel> result(static_cast<std::size_t>(outputWidth) * outputHeight);
		const auto* mappedBytes{ static_cast<const std::uint8_t*>(readback.GetConstMappedRange()) };
		for (std::uint32_t y{}; y < outputHeight; ++y)
			std::memcpy(result.data() + static_cast<std::size_t>(y) * outputWidth,
				mappedBytes + static_cast<std::size_t>(y) * bytesPerRow,
				static_cast<std::size_t>(outputWidth) * sizeof(Pixel));
		readback.Unmap();
		return result;
	}

	static inline wgpu::Instance s_instance{};
	static inline wgpu::Device s_device{};
};

TEST_F(SpriteShaderTest, RetainedUploadMatchesDynamicAtlasTintScaleAndLocalInstanceIndices)
{
	const std::vector<Pixel> atlas{
		{ 0, 0, 255, 255 }, { 255, 255, 255, 255 }, { 0, 255, 0, 0 },
		{ 0, 0, 255, 255 }, { 255, 255, 255, 255 }, { 0, 255, 0, 0 }
	};
	auto budget{ std::make_shared<ax::TaskBufferBudget>() };
	auto records{ std::make_shared<ax::SpriteBuffer>(2, budget) };
	ax::SpriteData tile{};
	tile.pos = { 8.25f, 8.25f };
	tile.scale = { 17.5f, 17.5f };
	tile.region = { 0, 0, 1, 2 };
	tile.useRegion = 1;
	tile.screenSpace = 1;
	tile.tint = { 1, 0.25f, 0.5f, 1 };
	records->set(1, tile);
	auto neighbour{ tile };
	neighbour.pos.x += 17.5f;
	records->set(2, neighbour);
	records->seal();
	const auto retained{ ax::upload_static_sprites(s_device, *records, { 1, 0 }) };
	tile.region.x = 1;
	neighbour.region.x = 1;
	const std::vector<ax::SpriteData> dynamic{ tile, neighbour };
	const auto expected{ render(atlas, 3, 2, dynamic, "fs_opaque") };
	EXPECT_EQ(render(atlas, 3, 2, dynamic, "fs_opaque", retained), expected);
	EXPECT_EQ(render(atlas, 3, 2, dynamic, "fs_opaque", retained), expected);
	EXPECT_EQ(records->get(1).region.x, 0);
}

TEST_F(SpriteShaderTest, AtlasTileEdgesStayOpaqueAtFractionalScale)
{
	const std::vector<Pixel> atlas{
		{ 0, 0, 255, 255 }, { 255, 0, 0, 255 }, { 0, 255, 0, 0 },
		{ 0, 0, 255, 255 }, { 255, 0, 0, 255 }, { 0, 255, 0, 0 },
		{ 0, 0, 255, 255 }, { 0, 0, 255, 255 }, { 0, 255, 0, 0 }
	};
	ax::SpriteData tile{};
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
	ax::SpriteData sprite{};
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

TEST_F(SpriteShaderTest, DistanceFieldSpritesHaveSharpAntialiasedEdges)
{
	// A horizontal distance ramp with the 0.5 edge in the middle, magnified 16 times.
	std::vector<Pixel> ramp{};
	for (std::uint8_t i{ 0 }; i < 4; ++i)
		ramp.push_back({ 255, 255, 255, static_cast<std::uint8_t>(32 + i * 64) });
	ax::SpriteData sprite{};
	sprite.pos = { 0, 16 };
	sprite.scale = { 16, 16 };
	sprite.screenSpace = 1;
	const auto countPartial = [](const std::vector<Pixel>& image)
	{
		int partial{};
		for (std::uint32_t x{ 0 }; x < s_size; ++x)
		{
			const auto alpha{ image[24 * s_size + x][3] };
			if (alpha > 0 && alpha < 255)
				++partial;
		}
		return partial;
	};
	const auto plain{ render(ramp, 4, 1, { sprite }) };
	const auto sdf{ render_sdf(ramp, 4, 1, { ax::pack_sdf_sprite(sprite, {}, false) }) };
	ASSERT_EQ(sdf.size(), s_size * s_size);
	// Inside the edge is fully opaque, outside is empty, and only about a pixel in between is blended.
	EXPECT_EQ(sdf[24 * s_size + 60][3], 255);
	EXPECT_EQ(sdf[24 * s_size + 4][3], 0);
	EXPECT_GE(countPartial(sdf), 1);
	EXPECT_LE(countPartial(sdf), 3);
	EXPECT_GT(countPartial(plain), 20);
}

TEST_F(SpriteShaderTest, MsdfEffectsExpandAndOffsetGlyphCoverage)
{
	std::vector<Pixel> ramp{};
	for (std::uint8_t i{ 0 }; i < 4; ++i)
	{
		const auto distance{ static_cast<std::uint8_t>(32 + i * 64) };
		ramp.push_back({ distance, distance, distance, 255 });
	}
	ax::SpriteData sprite{};
	sprite.pos = { 0, 16 };
	sprite.scale = { 16, 16 };
	sprite.screenSpace = 1;
	ax::SpriteEffect effect{};
	const auto plain{ render_sdf(ramp, 4, 1, { ax::pack_sdf_sprite(sprite, effect, true) }) };
	effect = { 1, 4.0f, 0.0f, {} };
	const auto outline{ render_sdf(ramp, 4, 1, { ax::pack_sdf_sprite(sprite, effect, true) }) };
	const auto coverage = [](const std::vector<Pixel>& image)
	{
		return std::ranges::count_if(image, [](const Pixel& pixel) { return pixel[3] > 0; });
	};
	ASSERT_EQ(plain.size(), s_size * s_size);
	EXPECT_GT(coverage(outline), coverage(plain));
	effect = { 2, 0.0f, 0.0f, { 2.0f, 0.0f } };
	const auto shadow{ render_sdf(ramp, 4, 1, { ax::pack_sdf_sprite(sprite, effect, true) }) };
	EXPECT_NE(shadow, plain);
}

TEST_F(SpriteShaderTest, WritesSdfFontRenderSamples)
{
	char* settingValue{};
	std::size_t settingLength{};
	ASSERT_EQ(_dupenv_s(&settingValue, &settingLength, "AXENG_SDF_FONT_SAMPLES"), 0);
	const std::string setting{ settingValue != nullptr ? settingValue : "" };
	std::free(settingValue);
	if (setting.empty() || setting == "0")
		GTEST_SKIP() << "Set AXENG_SDF_FONT_SAMPLES=1 to write SDF font comparison images";

	const std::filesystem::path outputDirectory{
		setting == "1" || setting == "true"
			? std::filesystem::temp_directory_path() / "AxEngSdfFontSamples"
			: std::filesystem::path(setting) };
	std::error_code filesystemError{};
	std::filesystem::create_directories(outputDirectory, filesystemError);
	ASSERT_FALSE(filesystemError) << filesystemError.message();

	constexpr std::uint32_t width{ 320 };
	constexpr std::uint32_t height{ 48 };
	const std::array<double, 4> steamBackground{ 0.82, 0.85, 0.91, 1.0 };
	constexpr std::array fontSizes{ 8u, 10u, 12u, 16u };
	const auto fontData{ ax::main_font_data() };
	std::vector<Pixel> baselineSdf16{};
	std::array<std::vector<Pixel>, 4> baselineSdfSamples{};
	std::array<std::vector<Pixel>, 4> msdfSamples{};
	const auto writeSample = [&](std::string_view name, const ax::SdfFont& font,
		std::uint32_t fontSize, std::array<double, 4> background, glm::vec4 foreground,
		float shadowOpacity, std::uint32_t supersampleFactor, bool msdf)
		-> std::vector<Pixel>
	{
		const float fontScale{ static_cast<float>(fontSize) / ax::sdf_font_reference_height };
		const float textScale{ fontScale * supersampleFactor };
		const float baseline{ ((height - fontSize) * 0.5f + font.baseline * fontScale) * supersampleFactor };
		const auto renderWidth{ width * supersampleFactor };
		const auto renderHeight{ height * supersampleFactor };
		const float centerX{ static_cast<float>(width) * supersampleFactor * 0.5f };
		std::vector<ax::SdfSpriteGpuData> sprites{};
		if (shadowOpacity > 0.0f)
			sprites = make_sdf_text(font, "Steam 168/200", centerX + static_cast<float>(supersampleFactor),
				baseline + static_cast<float>(supersampleFactor), textScale,
				{ 0.0f, 0.0f, 0.0f, shadowOpacity }, msdf);
		auto foregroundSprites{ make_sdf_text(font, "Steam 168/200", centerX, baseline,
			textScale, foreground, msdf) };
		sprites.insert(sprites.end(), foregroundSprites.begin(), foregroundSprites.end());

		std::vector<Pixel> atlasPixels(font.pixels.size() / sizeof(Pixel));
		std::memcpy(atlasPixels.data(), font.pixels.data(), font.pixels.size());
		const auto rendered{ render_sdf(atlasPixels, font.width, font.height, sprites, "fs_main",
				renderWidth, renderHeight, background, true) };
		if (rendered.size() != static_cast<std::size_t>(renderWidth) * renderHeight)
			return {};

		std::vector<Pixel> image(static_cast<std::size_t>(width) * height);
		const auto sampleCount{ supersampleFactor * supersampleFactor };
		for (std::uint32_t y{}; y < height; ++y)
		{
			for (std::uint32_t x{}; x < width; ++x)
			{
				std::array<std::uint32_t, 4> channels{};
				for (std::uint32_t sy{}; sy < supersampleFactor; ++sy)
				{
					for (std::uint32_t sx{}; sx < supersampleFactor; ++sx)
					{
						const auto& sample{ rendered[static_cast<std::size_t>(y * supersampleFactor + sy) *
							renderWidth + x * supersampleFactor + sx] };
						for (std::size_t channel{}; channel < channels.size(); ++channel)
							channels[channel] += sample[channel];
					}
				}
				auto& pixel{ image[static_cast<std::size_t>(y) * width + x] };
				for (std::size_t channel{}; channel < channels.size(); ++channel)
					pixel[channel] = static_cast<std::uint8_t>((channels[channel] + sampleCount / 2) / sampleCount);
			}
		}

		if (name == "size_16px_sdf_default_2x_downsample.png")
			baselineSdf16 = image;
		if (name == "size_16px_msdfgen_96x16_2x_downsample.png" &&
			baselineSdf16.size() == image.size())
		{
			std::uint64_t uprightDifference{};
			std::uint64_t flippedDifference{};
			for (std::uint32_t y{}; y < height; ++y)
			{
					for (std::uint32_t x{}; x < width; ++x)
					{
						const auto index{ static_cast<std::size_t>(y) * width + x };
						const auto flippedIndex{ static_cast<std::size_t>(height - y - 1) * width + x };
						for (std::size_t channel{}; channel < 3; ++channel)
						{
							uprightDifference += static_cast<std::uint64_t>(
								std::abs(static_cast<int>(baselineSdf16[index][channel]) - image[index][channel]));
							flippedDifference += static_cast<std::uint64_t>(
								std::abs(static_cast<int>(baselineSdf16[index][channel]) - image[flippedIndex][channel]));
						}
					}
			}
			EXPECT_LT(uprightDifference, flippedDifference) << "MSDF glyphs appear vertically flipped";
		}

		if (msdf)
		{
			const auto backgroundPixel{ image.front() };
			const auto hasVisibleText{ std::any_of(image.begin(), image.end(), [&](const Pixel& pixel)
			{
				return pixel != backgroundPixel;
			}) };
			EXPECT_TRUE(hasVisibleText) << name;
		}

		const auto path{ outputDirectory / std::string(name) };
		const auto result{ ax::write_png(path, width, height, 4, image.data(),
			width * static_cast<std::uint32_t>(sizeof(Pixel))) };
		EXPECT_EQ(result, ax::Error::Success) << path.string();
		return result == ax::Error::Success ? std::move(image) : std::vector<Pixel>{};
	};

	constexpr glm::vec4 white{ 1.0f, 1.0f, 1.0f, 1.0f };
	constexpr float thickening{ 0.5f };
	ax::SdfFont baselineSdf{};
	ASSERT_EQ(ax::generate_sdf_font(fontData, baselineSdf), ax::Error::Success);
	ax::SdfFont thickenedSdf{};
	ASSERT_EQ(ax::generate_sdf_font(fontData, thickenedSdf,
		ax::sdf_font_pixel_height, ax::sdf_font_spread, thickening), ax::Error::Success);
	ax::SdfFont msdf{};
	const ax::MsdfFontSettings baselineSettings{};
	ASSERT_EQ(ax::generate_msdf_font(fontData, msdf, baselineSettings), ax::Error::Success);
	ax::SdfFont thickenedMsdf{};
	auto thickenedSettings{ baselineSettings };
	thickenedSettings.thickness = thickening;
	ASSERT_EQ(ax::generate_msdf_font(fontData, thickenedMsdf, thickenedSettings), ax::Error::Success);
	ax::SdfFont narrowRangeMsdf{};
	auto narrowRangeSettings{ baselineSettings };
	narrowRangeSettings.spread = 8;
	ASSERT_EQ(ax::generate_msdf_font(fontData, narrowRangeMsdf, narrowRangeSettings), ax::Error::Success);
	ax::SdfFont highResolutionMsdf{};
	auto highResolutionSettings{ baselineSettings };
	highResolutionSettings.pixelHeight = 192.0f;
	highResolutionSettings.spread = 32;
	ASSERT_EQ(ax::generate_msdf_font(fontData, highResolutionMsdf, highResolutionSettings),
		ax::Error::Success);
	ax::SdfFont simpleColoredMsdf{};
	auto simpleColoringSettings{ baselineSettings };
	simpleColoringSettings.edgeColoring = ax::MsdfEdgeColoring::Simple;
	ASSERT_EQ(ax::generate_msdf_font(fontData, simpleColoredMsdf, simpleColoringSettings),
		ax::Error::Success);
	ax::SdfFont noErrorCorrectionMsdf{};
	auto noErrorCorrectionSettings{ baselineSettings };
	noErrorCorrectionSettings.errorCorrection = ax::MsdfErrorCorrection::Disabled;
	ASSERT_EQ(ax::generate_msdf_font(fontData, noErrorCorrectionMsdf, noErrorCorrectionSettings),
		ax::Error::Success);
	ax::SdfFont edgeOnlyCorrectionMsdf{};
	auto edgeOnlyCorrectionSettings{ baselineSettings };
	edgeOnlyCorrectionSettings.errorCorrection = ax::MsdfErrorCorrection::EdgeOnly;
	ASSERT_EQ(ax::generate_msdf_font(fontData, edgeOnlyCorrectionMsdf, edgeOnlyCorrectionSettings),
		ax::Error::Success);

	struct Treatment
	{
		std::string_view name;
		std::string_view label;
		const ax::SdfFont* font;
		bool msdf;
		std::uint32_t supersampleFactor;
	};
	const std::array treatments{
		Treatment{ "sdf_default_2x_downsample", "SDF, 2x", &baselineSdf, false, 2 },
		Treatment{ "sdf_thickened_0.5px_2x_downsample", "SDF +0.5px, 2x",
			&thickenedSdf, false, 2 },
		Treatment{ "msdfgen_96x16_2x_downsample", "MSDF distance-color, default",
			&msdf, true, 2 },
		Treatment{ "msdfgen_96x16_native_1x", "MSDF, no supersampling",
			&msdf, true, 1 },
		Treatment{ "msdfgen_96x16_4x_downsample", "MSDF, 4x downsample",
			&msdf, true, 4 },
		Treatment{ "msdfgen_96x8_narrow_range_2x_downsample",
			"MSDF, narrow range", &narrowRangeMsdf, true, 2 },
		Treatment{ "msdfgen_192x32_high_resolution_2x_downsample",
			"MSDF, high-res atlas", &highResolutionMsdf, true, 2 },
		Treatment{ "msdfgen_96x16_simple_coloring_2x_downsample",
			"MSDF, simple edge colors", &simpleColoredMsdf, true, 2 },
		Treatment{ "msdfgen_96x16_no_error_correction_2x_downsample",
			"MSDF, no correction", &noErrorCorrectionMsdf, true, 2 },
		Treatment{ "msdfgen_96x16_edge_only_correction_2x_downsample",
			"MSDF, edge-only correction", &edgeOnlyCorrectionMsdf, true, 2 },
		Treatment{ "msdfgen_96x16_thickened_0.5px_2x_downsample",
			"MSDF +0.5px, 2x", &thickenedMsdf, true, 2 }
	};
	std::array<std::array<std::vector<Pixel>, fontSizes.size()>, treatments.size()> treatmentSamples{};
	for (std::size_t sizeIndex{}; sizeIndex < fontSizes.size(); ++sizeIndex)
	{
		for (std::size_t treatmentIndex{}; treatmentIndex < treatments.size(); ++treatmentIndex)
		{
			const auto& treatment{ treatments[treatmentIndex] };
			const auto fontSize{ fontSizes[sizeIndex] };
			const std::string name{ "size_" + std::to_string(fontSize) + "px_" +
				std::string(treatment.name) + ".png" };
			auto image{ writeSample(name, *treatment.font, fontSize, steamBackground, white, 0.3f,
				treatment.supersampleFactor, treatment.msdf) };
			ASSERT_EQ(image.size(), static_cast<std::size_t>(width) * height) << name;
			treatmentSamples[treatmentIndex][sizeIndex] = std::move(image);
			if (treatment.name == "sdf_default_2x_downsample")
				baselineSdfSamples[sizeIndex] = treatmentSamples[treatmentIndex][sizeIndex];
			else if (treatment.name == "msdfgen_96x16_2x_downsample")
				msdfSamples[sizeIndex] = treatmentSamples[treatmentIndex][sizeIndex];
		}
	}

	constexpr std::uint32_t labelColumnWidth{ 200 };
	const auto settingsSheetWidth{ labelColumnWidth +
		width * static_cast<std::uint32_t>(fontSizes.size()) };
	const auto settingsSheetHeight{ height * static_cast<std::uint32_t>(treatments.size()) };
	std::vector<Pixel> settingsSheet(static_cast<std::size_t>(settingsSheetWidth) * settingsSheetHeight);
	std::vector<Pixel> atlasPixels(msdf.pixels.size() / sizeof(Pixel));
	std::memcpy(atlasPixels.data(), msdf.pixels.data(), msdf.pixels.size());
	for (std::size_t treatmentIndex{}; treatmentIndex < treatments.size(); ++treatmentIndex)
	{
		constexpr float labelFontSize{ 9.0f };
		const float labelScale{ labelFontSize / ax::sdf_font_reference_height };
		const float labelBaseline{ (height - labelFontSize) * 0.5f + msdf.baseline * labelScale };
		constexpr glm::vec4 labelColor{ 0.12f, 0.15f, 0.2f, 1.0f };
		const auto labelSprites{ make_sdf_text(msdf, treatments[treatmentIndex].label,
			labelColumnWidth * 0.5f, labelBaseline, labelScale, labelColor, true) };
		const auto labelImage{ render_sdf(atlasPixels, msdf.width, msdf.height, labelSprites, "fs_main",
			labelColumnWidth, height, steamBackground, true) };
		ASSERT_EQ(labelImage.size(), static_cast<std::size_t>(labelColumnWidth) * height)
			<< treatments[treatmentIndex].label;
		for (std::size_t sizeIndex{}; sizeIndex < fontSizes.size(); ++sizeIndex)
		{
			const auto& sample{ treatmentSamples[treatmentIndex][sizeIndex] };
			ASSERT_EQ(sample.size(), static_cast<std::size_t>(width) * height)
				<< treatments[treatmentIndex].name;
			for (std::uint32_t y{}; y < height; ++y)
			{
				const auto sourceOffset{ static_cast<std::size_t>(y) * width };
				const auto targetOffset{ (static_cast<std::size_t>(treatmentIndex) * height + y) *
					settingsSheetWidth + labelColumnWidth + sizeIndex * width };
				std::copy_n(sample.begin() + sourceOffset, width, settingsSheet.begin() + targetOffset);
			}
		}
		for (std::uint32_t y{}; y < height; ++y)
		{
			const auto sourceOffset{ static_cast<std::size_t>(y) * labelColumnWidth };
			const auto targetOffset{ (treatmentIndex * height + y) * settingsSheetWidth };
			std::copy_n(labelImage.begin() + sourceOffset, labelColumnWidth,
				settingsSheet.begin() + targetOffset);
		}
	}
	const auto settingsSheetPath{ outputDirectory / "msdfgen_settings_comparison.png" };
	const auto settingsSheetResult{ ax::write_png(settingsSheetPath, settingsSheetWidth, settingsSheetHeight,
		4, settingsSheet.data(), settingsSheetWidth * static_cast<std::uint32_t>(sizeof(Pixel))) };
	ASSERT_EQ(settingsSheetResult, ax::Error::Success) << settingsSheetPath.string();

	const auto comparisonWidth{ width * static_cast<std::uint32_t>(fontSizes.size()) };
	constexpr std::uint32_t comparisonRows{ 3 };
	const auto comparisonHeight{ height * comparisonRows };
	std::vector<Pixel> comparison(static_cast<std::size_t>(comparisonWidth) * comparisonHeight);
	constexpr std::uint32_t differenceAmplification{ 64 };
	std::uint64_t totalRgbDifference{};
	for (std::size_t sizeIndex{}; sizeIndex < fontSizes.size(); ++sizeIndex)
	{
		ASSERT_EQ(baselineSdfSamples[sizeIndex].size(), static_cast<std::size_t>(width) * height);
		ASSERT_EQ(msdfSamples[sizeIndex].size(), static_cast<std::size_t>(width) * height);
		for (std::uint32_t y{}; y < height; ++y)
		{
			for (std::uint32_t x{}; x < width; ++x)
			{
				const auto sourceIndex{ static_cast<std::size_t>(y) * width + x };
				const auto targetIndex{ static_cast<std::size_t>(y) * comparisonWidth + sizeIndex * width + x };
				const auto& sdfPixel{ baselineSdfSamples[sizeIndex][sourceIndex] };
				const auto& msdfPixel{ msdfSamples[sizeIndex][sourceIndex] };
				comparison[targetIndex] = sdfPixel;
				comparison[targetIndex + static_cast<std::size_t>(height) * comparisonWidth] = msdfPixel;
				Pixel differencePixel{ 0, 0, 0, 255 };
				for (std::size_t channel{}; channel < 3; ++channel)
				{
					const auto difference{ static_cast<std::uint32_t>(
						std::abs(static_cast<int>(sdfPixel[channel]) - msdfPixel[channel])) };
					totalRgbDifference += difference;
					differencePixel[channel] = static_cast<std::uint8_t>(
						std::min(255u, difference * differenceAmplification));
				}
				comparison[targetIndex + static_cast<std::size_t>(height) * comparisonWidth * 2] =
					differencePixel;
			}
		}
	}

	const auto comparisonPath{ outputDirectory / "sdf_msdf_direct_comparison.png" };
	const auto comparisonResult{ ax::write_png(comparisonPath, comparisonWidth, comparisonHeight, 4,
		comparison.data(), comparisonWidth * static_cast<std::uint32_t>(sizeof(Pixel))) };
	ASSERT_EQ(comparisonResult, ax::Error::Success) << comparisonPath.string();
	testing::Test::RecordProperty("sdf_sample_output_directory", outputDirectory.string());
	GTEST_SUCCEED() << "Wrote MSDF settings contact sheet to " << settingsSheetPath.string()
		<< " and SDF / MSDF / 64x RGB difference image to " << comparisonPath.string()
		<< " (columns: 8, 10, 12, 16 px; total raw RGB difference: " << totalRgbDifference << ")";
}

TEST_F(SpriteShaderTest, RotatedScaledSpritePreservesColourAndTint)
{
	ax::SpriteData sprite{};
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

TEST(SpritePacking, RegionTintAndClipRoundTrip)
{
	ax::SpriteData data{};
	data.useRegion = ax::sprite_flag_region;
	data.region = { 12, 34, 56, 78 };
	data.tint = { 1.0f, 0.5f, 0.25f, 2.0f };
	data.clipRect = { 3, 4, 500, 600 };
	const auto packed{ ax::pack_sprite(data) };
	EXPECT_EQ(packed.region, glm::uvec2(12u | 34u << 16, 56u | 78u << 16));
	EXPECT_EQ(glm::unpackHalf2x16(packed.tint.x), glm::vec2(1.0f, 0.5f));
	EXPECT_EQ(glm::unpackHalf2x16(packed.tint.y), glm::vec2(0.25f, 2.0f));
	EXPECT_EQ(packed.clip, glm::uvec2(3u | 4u << 16, 500u | 600u << 16));
}

TEST(SpritePacking, UnsetRegionAndClipMeanWholeTextureAndNoClip)
{
	const auto packed{ ax::pack_sprite(ax::SpriteData{}) };
	EXPECT_EQ(packed.region, glm::uvec2(0u));
	EXPECT_EQ(packed.clip, glm::uvec2(0u, 0xFFFFFFFFu));
	// A region is ignored unless the sprite asks for it.
	ax::SpriteData data{};
	data.region = { 1, 2, 3, 4 };
	EXPECT_EQ(ax::pack_sprite(data).region, glm::uvec2(0u));
}

TEST(SpritePacking, DistanceFieldEffectRoundTrips)
{
	const auto packed{ ax::pack_sdf_sprite({}, { 2, 1.5f, 0.5f, { -2.0f, 3.0f } }, true) };
	EXPECT_EQ(packed.flags, 1u | 2u << 1);
	EXPECT_EQ(glm::unpackHalf2x16(packed.effect), glm::vec2(1.5f, 0.5f));
	EXPECT_EQ(glm::unpackHalf2x16(packed.effectOffset), glm::vec2(-2.0f, 3.0f));
}
