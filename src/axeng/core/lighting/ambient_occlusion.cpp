#include "axeng/core/lighting/ambient_occlusion.h"
#include "axeng/core/lighting/light_renderer.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
	// #embed paths are resolved relative to this file. The data is not null terminated.
	constexpr unsigned char s_ambient_occlusion_shader[] = {
#embed "ambient_occlusion.wgsl"
	};

	// Coverage only: the silhouettes in one channel, the blurred shadows of the two sets in two, and the light's factor
	// in one.
	constexpr auto s_mask_format{ ax::ao_mask_format };
	constexpr auto s_blur_format{ wgpu::TextureFormat::RG8Unorm };
	constexpr auto s_factor_format{ wgpu::TextureFormat::R8Unorm };
	// The blur reaches at most this many steps either side; wider shadows take longer steps.
	constexpr float s_max_steps{ 24.0f };

	struct Uniforms
	{
		glm::vec4 mask{};
		glm::vec4 screen{};
		glm::vec4 blur{};
		glm::vec4 shade{};
	};
	static_assert(sizeof(Uniforms) == 64);
}

bool ax::AmbientOcclusion::init(const wgpu::Device& device)
{
	if (m_applyPipeline)
		return true;
	wgpu::ShaderSourceWGSL source{};
	source.code = { reinterpret_cast<const char*>(s_ambient_occlusion_shader), sizeof(s_ambient_occlusion_shader) };
	wgpu::ShaderModuleDescriptor shaderDesc{};
	shaderDesc.nextInChain = &source;
	shaderDesc.label = "Ambient occlusion shader";
	const auto shader{ device.CreateShaderModule(&shaderDesc) };

	std::array<wgpu::BindGroupLayoutEntry, 6> entries{};
	entries[0].binding = 0;
	entries[0].visibility = wgpu::ShaderStage::Fragment;
	entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
	entries[0].buffer.minBindingSize = sizeof(Uniforms);
	for (const auto binding : { 1u, 2u, 4u, 5u })
	{
		entries[binding].binding = binding;
		entries[binding].visibility = wgpu::ShaderStage::Fragment;
		entries[binding].texture.sampleType = wgpu::TextureSampleType::Float;
		entries[binding].texture.viewDimension = wgpu::TextureViewDimension::e2D;
	}
	entries[3].binding = 3;
	entries[3].visibility = wgpu::ShaderStage::Fragment;
	entries[3].sampler.type = wgpu::SamplerBindingType::Filtering;
	wgpu::BindGroupLayoutDescriptor layoutDesc{};
	layoutDesc.entryCount = entries.size();
	layoutDesc.entries = entries.data();
	m_layout = device.CreateBindGroupLayout(&layoutDesc);
	wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{};
	pipelineLayoutDesc.bindGroupLayoutCount = 1;
	pipelineLayoutDesc.bindGroupLayouts = &m_layout;
	const auto layout{ device.CreatePipelineLayout(&pipelineLayoutDesc) };

	const auto pipeline{ [&](const char* fragment, wgpu::TextureFormat format, const wgpu::BlendState* blend)
	{
		wgpu::ColorTargetState target{};
		target.format = format;
		target.blend = blend;
		target.writeMask = wgpu::ColorWriteMask::All;
		wgpu::FragmentState fragmentState{};
		fragmentState.module = shader;
		fragmentState.entryPoint = fragment;
		fragmentState.targetCount = 1;
		fragmentState.targets = &target;
		wgpu::RenderPipelineDescriptor desc{};
		desc.label = fragment;
		desc.layout = layout;
		desc.vertex.module = shader;
		desc.vertex.entryPoint = "vs_fullscreen";
		desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
		desc.multisample.count = 1;
		desc.fragment = &fragmentState;
		return device.CreateRenderPipeline(&desc);
	} };
	m_blurFirstPipeline = pipeline("fs_blur_first", s_blur_format, nullptr);
	m_blurSecondPipeline = pipeline("fs_blur_second", s_blur_format, nullptr);
	m_applyPipeline = pipeline("fs_apply", s_factor_format, nullptr);

	wgpu::SamplerDescriptor samplerDesc{};
	samplerDesc.magFilter = wgpu::FilterMode::Linear;
	samplerDesc.minFilter = wgpu::FilterMode::Linear;
	samplerDesc.addressModeU = wgpu::AddressMode::ClampToEdge;
	samplerDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
	m_sampler = device.CreateSampler(&samplerDesc);
	for (auto& buffer : m_uniforms)
	{
		wgpu::BufferDescriptor desc{};
		desc.label = "Ambient occlusion uniforms";
		desc.size = sizeof(Uniforms);
		desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
		buffer = gpu::create_buffer(device, desc, "Lighting");
	}
	return m_applyPipeline != nullptr;
}

bool ax::AmbientOcclusion::ensure_targets(const wgpu::Device& device, std::uint32_t width, std::uint32_t height)
{
	if (m_width == width && m_height == height && m_mask[0].texture)
		return true;
	const auto make{ [&](const char* label, wgpu::TextureFormat format = s_mask_format)
	{
		wgpu::TextureDescriptor desc{};
		desc.label = label;
		desc.size = { width, height };
		desc.format = format;
		desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment;
		Target target{ gpu::create_texture(device, desc, "Lighting"), {} };
		if (target.texture)
			target.view = target.texture.CreateView();
		return target;
	} };
	m_mask[0] = make("Ambient occlusion mask");
	m_mask[1] = make("Ambient occlusion stand-in mask");
	m_silhouettes[0] = make("Ambient occlusion silhouettes");
	m_silhouettes[1] = make("Ambient occlusion stand-in silhouettes");
	m_blur[0] = make("Ambient occlusion blur A", s_blur_format);
	m_blur[1] = make("Ambient occlusion blur B", s_blur_format);
	m_width = width;
	m_height = height;
	m_applyGroup = {};
	return m_mask[0].view && m_mask[1].view && m_silhouettes[0].view && m_silhouettes[1].view && m_blur[0].view
		&& m_blur[1].view;
}

wgpu::BindGroup ax::AmbientOcclusion::bind(const wgpu::Device& device, const gpu::TrackedBuffer& uniforms,
	const wgpu::TextureView& source, const wgpu::TextureView& standIns, const wgpu::TextureView& spriteSilhouettes,
	const wgpu::TextureView& standInSilhouettes) const
{
	std::array<wgpu::BindGroupEntry, 6> entries{};
	entries[0].binding = 0;
	entries[0].buffer = uniforms;
	entries[0].size = sizeof(Uniforms);
	entries[1].binding = 1;
	entries[1].textureView = source;
	entries[2].binding = 2;
	entries[2].textureView = standIns;
	entries[3].binding = 3;
	entries[3].sampler = m_sampler;
	entries[4].binding = 4;
	entries[4].textureView = spriteSilhouettes;
	entries[5].binding = 5;
	entries[5].textureView = standInSilhouettes;
	wgpu::BindGroupDescriptor desc{};
	desc.layout = m_layout;
	desc.entryCount = entries.size();
	desc.entries = entries.data();
	return device.CreateBindGroup(&desc);
}

bool ax::AmbientOcclusion::encode(const wgpu::Device& device, const wgpu::Queue& queue, wgpu::CommandEncoder& encoder,
	const LightView& screen, std::uint32_t divisor, std::uint32_t mapWidth, std::uint32_t mapHeight,
	const AmbientOcclusion2D& settings, const CasterDraw& casters, GpuPassTimer* timer)
{
	ZoneScopedN("AmbientOcclusion::encode");
	if (!casters || !m_applyPipeline || settings.darkness <= 0.0f || screen.viewport.x <= 0.0f || screen.viewport.y <= 0.0f
		|| !(screen.zoom > 0.0f))
		return false;
	// The mask reaches past the screen's edges by as far as a shadow can fall onto it from outside, in screen pixels.
	const auto radius{ std::max(settings.radius, 0.0f) };
	const auto margin{ std::ceil((glm::length(settings.offset) + radius * 1.5f) * screen.zoom) };
	const auto resolution{ std::clamp(settings.resolution, 1.0f, 8.0f) };
	const glm::vec2 covered{ screen.viewport + glm::vec2{ margin * 2.0f } };
	const auto width{ std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::ceil(covered.x / resolution))) };
	const auto height{ std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::ceil(covered.y / resolution))) };
	if (!ensure_targets(device, width, height))
		return false;
	if (!m_factor.texture || m_factorWidth != mapWidth || m_factorHeight != mapHeight)
	{
		wgpu::TextureDescriptor desc{};
		desc.label = "Ambient occlusion factor";
		desc.size = { std::max<std::uint32_t>(mapWidth, 1), std::max<std::uint32_t>(mapHeight, 1) };
		desc.format = s_factor_format;
		desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment;
		m_factor = { gpu::create_texture(device, desc, "Lighting"), {} };
		if (!m_factor.texture)
			return false;
		m_factor.view = m_factor.texture.CreateView();
		m_factorWidth = mapWidth;
		m_factorHeight = mapHeight;
	}

	const auto beginPass{ [&](const wgpu::TextureView& target, const char* label)
	{
		wgpu::RenderPassColorAttachment attachment{};
		attachment.view = target;
		attachment.depthSlice = wgpu::kDepthSliceUndefined;
		attachment.loadOp = wgpu::LoadOp::Clear;
		attachment.storeOp = wgpu::StoreOp::Store;
		attachment.clearValue = { 0.0, 0.0, 0.0, 0.0 };
		wgpu::RenderPassDescriptor desc{};
		desc.label = label;
		desc.colorAttachmentCount = 1;
		desc.colorAttachments = &attachment;
		if (timer)
			desc.timestampWrites = timer->pass("GPU ms: ambient occlusion");
		return encoder.BeginRenderPass(&desc);
	} };

	// The silhouettes over the whole covered area: a camera there shows the world shifted by the offset.
	const LightView covering{ .viewport = covered, .cameraPosition = screen.cameraPosition, .zoom = screen.zoom };
	const LightView shifted{ .viewport = covering.viewport, .cameraPosition = covering.cameraPosition - settings.offset,
		.zoom = covering.zoom };
	for (std::uint32_t set{}; set < 2; ++set)
	{
		{
			auto pass{ beginPass(m_mask[set].view, set == 0 ? "Ambient occlusion mask" : "Ambient occlusion stand-in mask") };
			casters(pass, shifted, set * 2);
			pass.End();
		}
		auto pass{ beginPass(m_silhouettes[set].view,
			set == 0 ? "Ambient occlusion silhouettes" : "Ambient occlusion stand-in silhouettes") };
		if (settings.selfDarkness < 1.0f)
			casters(pass, covering, set * 2 + 1);
		pass.End();
	}

	// Blurred both ways; the sigma is half the radius, in mask texels.
	const auto sigma{ std::max(radius * 0.5f * screen.zoom / resolution, 0.001f) };
	const auto reach{ sigma * 2.5f };
	const auto step{ std::max(1.0f, reach / s_max_steps) };
	const auto steps{ std::ceil(reach / step) };
	const glm::vec4 mask{ static_cast<float>(width), static_cast<float>(height), 1.0f / static_cast<float>(width),
		1.0f / static_cast<float>(height) };
	const glm::vec4 shade{ settings.darkness, std::clamp(settings.selfDarkness, 0.0f, 1.0f), 0.0f, 0.0f };
	const Uniforms horizontal{ mask, {}, { step, 0.0f, sigma / step, steps }, shade };
	const Uniforms vertical{ mask, {}, { 0.0f, step, sigma / step, steps }, shade };
	const Uniforms apply{ mask,
		{ static_cast<float>(std::max<std::uint32_t>(divisor, 1)), margin, screen.viewport.x, screen.viewport.y },
		{}, shade };
	queue.WriteBuffer(m_uniforms[0], 0, &horizontal, sizeof(Uniforms));
	queue.WriteBuffer(m_uniforms[1], 0, &vertical, sizeof(Uniforms));
	queue.WriteBuffer(m_uniforms[2], 0, &apply, sizeof(Uniforms));
	const auto blurPass{ [&](const wgpu::RenderPipeline& pipeline, const wgpu::TextureView& target,
		const wgpu::BindGroup& group, const char* label)
	{
		auto pass{ beginPass(target, label) };
		pass.SetPipeline(pipeline);
		pass.SetBindGroup(0, group);
		pass.Draw(3);
		pass.End();
	} };
	blurPass(m_blurFirstPipeline, m_blur[0].view, bind(device, m_uniforms[0], m_mask[0].view, m_mask[1].view,
		m_silhouettes[0].view, m_silhouettes[1].view), "Ambient occlusion blur across");
	blurPass(m_blurSecondPipeline, m_blur[1].view, bind(device, m_uniforms[1], m_blur[0].view, m_mask[1].view,
		m_silhouettes[0].view, m_silhouettes[1].view), "Ambient occlusion blur down");
	if (!m_applyGroup)
		m_applyGroup = bind(device, m_uniforms[2], m_blur[1].view, m_mask[1].view, m_silhouettes[0].view,
			m_silhouettes[1].view);
	blurPass(m_applyPipeline, m_factor.view, m_applyGroup, "Ambient occlusion factor");
	return true;
}
