#include "axeng/core/lighting/light_renderer.h"

#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>

#include <algorithm>
#include <array>
#include <bit>

namespace
{
	// #embed paths are resolved relative to this file. The data is not null terminated.
	constexpr unsigned char s_lighting_shader[] = {
#embed "lighting.wgsl"
	};

	constexpr auto s_light_map_format{ ax::light_map_format };
	// Each instance is an octagon drawn as six triangles (see corner() in lighting.wgsl).
	constexpr std::uint32_t s_instance_vertices{ 18 };

	struct Uniforms
	{
		glm::vec4 viewport{};
		glm::vec4 camera{};
		glm::vec4 ambient{};
	};
	static_assert(sizeof(Uniforms) == 48);

	wgpu::BlendState additive_blend()
	{
		wgpu::BlendState blend{};
		blend.color = { wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::One };
		blend.alpha = { wgpu::BlendOperation::Add, wgpu::BlendFactor::Zero, wgpu::BlendFactor::One };
		return blend;
	}
}

bool ax::lighting_active(const LightingSettings& settings, std::size_t lights, std::size_t glows) noexcept
{
	if (!settings.enabled)
		return false;
	const bool fullAmbient{ settings.ambient.r >= 1.0f && settings.ambient.g >= 1.0f && settings.ambient.b >= 1.0f };
	// Lights only show below the exposure limit, so a fully lit scene with no glows is left untouched.
	return !fullAmbient || (settings.maxExposure > 1.0f && lights > 0) || glows > 0;
}

bool ax::LightRenderer::init(const wgpu::Device& device, wgpu::TextureFormat targetFormat, std::uint32_t sampleCount)
{
	if (m_lightPipeline && m_device.Get() == device.Get() && m_targetFormat == targetFormat
		&& m_sampleCount == sampleCount)
		return true;
	m_device = device;
	m_targetFormat = targetFormat;
	m_sampleCount = sampleCount;
	m_lightMap.reset();
	m_applyBindGroup = {};
	m_lights = {};
	m_glows = {};

	wgpu::ShaderSourceWGSL source{};
	source.code = { reinterpret_cast<const char*>(s_lighting_shader), sizeof(s_lighting_shader) };
	wgpu::ShaderModuleDescriptor shaderDesc{};
	shaderDesc.nextInChain = &source;
	shaderDesc.label = "Lighting shader";
	const auto shader{ device.CreateShaderModule(&shaderDesc) };

	std::array<wgpu::BindGroupLayoutEntry, 2> instanceEntries{};
	instanceEntries[0].binding = 0;
	instanceEntries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
	instanceEntries[0].buffer.type = wgpu::BufferBindingType::Uniform;
	instanceEntries[0].buffer.minBindingSize = sizeof(Uniforms);
	instanceEntries[1].binding = 1;
	instanceEntries[1].visibility = wgpu::ShaderStage::Vertex;
	instanceEntries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
	wgpu::BindGroupLayoutDescriptor instanceLayoutDesc{};
	instanceLayoutDesc.entryCount = instanceEntries.size();
	instanceLayoutDesc.entries = instanceEntries.data();
	m_instanceLayout = device.CreateBindGroupLayout(&instanceLayoutDesc);

	std::array<wgpu::BindGroupLayoutEntry, 3> applyEntries{};
	applyEntries[0] = instanceEntries[0];
	applyEntries[1].binding = 1;
	applyEntries[1].visibility = wgpu::ShaderStage::Fragment;
	applyEntries[1].texture.sampleType = wgpu::TextureSampleType::Float;
	applyEntries[1].texture.viewDimension = wgpu::TextureViewDimension::e2D;
	applyEntries[2].binding = 2;
	applyEntries[2].visibility = wgpu::ShaderStage::Fragment;
	applyEntries[2].sampler.type = wgpu::SamplerBindingType::Filtering;
	wgpu::BindGroupLayoutDescriptor applyLayoutDesc{};
	applyLayoutDesc.entryCount = applyEntries.size();
	applyLayoutDesc.entries = applyEntries.data();
	m_applyLayout = device.CreateBindGroupLayout(&applyLayoutDesc);

	const auto pipeline{ [&](const wgpu::BindGroupLayout& groupLayout, const char* vertex, const char* fragment,
		wgpu::TextureFormat format, std::uint32_t samples, const wgpu::BlendState& blend)
	{
		wgpu::PipelineLayoutDescriptor layoutDesc{};
		layoutDesc.bindGroupLayoutCount = 1;
		layoutDesc.bindGroupLayouts = &groupLayout;
		wgpu::ColorTargetState target{};
		target.format = format;
		target.blend = &blend;
		target.writeMask = wgpu::ColorWriteMask::All;
		wgpu::FragmentState fragmentState{};
		fragmentState.module = shader;
		fragmentState.entryPoint = fragment;
		fragmentState.targetCount = 1;
		fragmentState.targets = &target;
		wgpu::RenderPipelineDescriptor desc{};
		desc.layout = device.CreatePipelineLayout(&layoutDesc);
		desc.vertex.module = shader;
		desc.vertex.entryPoint = vertex;
		desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
		desc.primitive.cullMode = wgpu::CullMode::None;
		desc.multisample.count = samples;
		desc.multisample.mask = ~0u;
		desc.fragment = &fragmentState;
		return device.CreateRenderPipeline(&desc);
	} };

	const auto additive{ additive_blend() };
	// Multiplies the target by the light (src * dst), keeping the target's alpha.
	wgpu::BlendState multiply{};
	multiply.color = { wgpu::BlendOperation::Add, wgpu::BlendFactor::Dst, wgpu::BlendFactor::Zero };
	multiply.alpha = { wgpu::BlendOperation::Add, wgpu::BlendFactor::Zero, wgpu::BlendFactor::One };
	// Unorm targets clamp the shader output to 1 before blending, so to brighten past 1 the shader outputs half the
	// light and the blend doubles it: src * dst + dst * src.
	wgpu::BlendState doubled{ multiply };
	doubled.color.dstFactor = wgpu::BlendFactor::Src;
	m_lightPipeline = pipeline(m_instanceLayout, "vs_light", "fs_light", s_light_map_format, 1, additive);
	m_glowPipeline = pipeline(m_instanceLayout, "vs_glow", "fs_glow", targetFormat, sampleCount, additive);
	m_applyPipeline = pipeline(m_applyLayout, "vs_apply", "fs_apply", targetFormat, sampleCount, multiply);
	m_brightApplyPipeline = pipeline(m_applyLayout, "vs_apply", "fs_apply_bright", targetFormat, sampleCount, doubled);

	wgpu::BufferDescriptor uniformDesc{};
	uniformDesc.size = sizeof(Uniforms);
	uniformDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform;
	uniformDesc.label = "Lighting uniforms";
	m_uniforms = device.CreateBuffer(&uniformDesc);

	wgpu::SamplerDescriptor samplerDesc{};
	samplerDesc.addressModeU = wgpu::AddressMode::ClampToEdge;
	samplerDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
	samplerDesc.magFilter = wgpu::FilterMode::Linear;
	samplerDesc.minFilter = wgpu::FilterMode::Linear;
	m_sampler = device.CreateSampler(&samplerDesc);

	if (!m_lightPipeline || !m_glowPipeline || !m_applyPipeline || !m_brightApplyPipeline)
	{
		spdlog::error("Failed to create the lighting pipelines.");
		m_lightPipeline = {};
		return false;
	}
	return true;
}

bool ax::LightRenderer::ensure_light_map(const wgpu::Device& device, std::uint32_t width, std::uint32_t height)
{
	if (m_lightMap && m_lightMap->width() == width && m_lightMap->height() == height)
		return true;
	m_lightMap = std::make_unique<Texture>(Badge<LightRenderer>{}, "light_map", width, height, s_light_map_format, &m_device);
	if (!m_lightMap->is_loaded())
	{
		m_lightMap.reset();
		return false;
	}

	std::array<wgpu::BindGroupEntry, 3> entries{};
	entries[0].binding = 0;
	entries[0].buffer = m_uniforms;
	entries[0].size = sizeof(Uniforms);
	entries[1].binding = 1;
	entries[1].textureView = m_lightMap->view();
	entries[2].binding = 2;
	entries[2].sampler = m_sampler;
	wgpu::BindGroupDescriptor groupDesc{};
	groupDesc.layout = m_applyLayout;
	groupDesc.entryCount = entries.size();
	groupDesc.entries = entries.data();
	m_applyBindGroup = device.CreateBindGroup(&groupDesc);
	return true;
}

void ax::LightRenderer::ensure_buffer(const wgpu::Device& device, InstanceBuffer& instances, std::size_t bytes,
	const char* label)
{
	if (instances.buffer && instances.capacity >= bytes)
		return;
	instances.capacity = std::bit_ceil(std::max<std::size_t>(bytes, 4096));
	wgpu::BufferDescriptor desc{};
	desc.size = instances.capacity;
	desc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Storage;
	desc.label = label;
	instances.buffer = device.CreateBuffer(&desc);
	// The whole buffer is bound, so the bind group only changes with the buffer; draws read just their instances.
	std::array<wgpu::BindGroupEntry, 2> entries{};
	entries[0].binding = 0;
	entries[0].buffer = m_uniforms;
	entries[0].size = sizeof(Uniforms);
	entries[1].binding = 1;
	entries[1].buffer = instances.buffer;
	entries[1].size = instances.capacity;
	wgpu::BindGroupDescriptor groupDesc{};
	groupDesc.layout = m_instanceLayout;
	groupDesc.entryCount = entries.size();
	groupDesc.entries = entries.data();
	instances.bindGroup = device.CreateBindGroup(&groupDesc);
}

void ax::LightRenderer::encode(const wgpu::Device& device, const wgpu::Queue& queue, wgpu::CommandEncoder& encoder,
	const wgpu::TextureView& target, std::uint32_t width, std::uint32_t height, const LightView& view,
	const LightingSettings& settings, std::span<const Light2D> lights, std::span<const Glow2D> glows,
	const std::function<void(wgpu::RenderPassEncoder&)>& extraLights)
{
	ZoneScopedN("LightRenderer::encode");
	m_stats = {};
	if (!m_lightPipeline || width == 0 || height == 0)
		return;
	const auto divisor{ std::clamp<std::uint32_t>(settings.resolutionDivisor, 1, 4) };
	const auto mapWidth{ std::max<std::uint32_t>(1, (width + divisor - 1) / divisor) };
	const auto mapHeight{ std::max<std::uint32_t>(1, (height + divisor - 1) / divisor) };
	if (!ensure_light_map(device, mapWidth, mapHeight))
		return;

	const Uniforms uniforms{
		.viewport = { view.viewport.x, view.viewport.y, 0.0f, 0.0f },
		.camera = { view.cameraPosition.x, view.cameraPosition.y, std::max(view.zoom, 0.001f), 0.0f },
		.ambient = { settings.ambient, std::clamp(settings.maxExposure, 0.0f, 2.0f) },
	};
	queue.WriteBuffer(m_uniforms, 0, &uniforms, sizeof(uniforms));

	{
		ZoneScopedN("LightRenderer::light-map");
		wgpu::RenderPassColorAttachment attachment{};
		attachment.view = m_lightMap->view();
		attachment.depthSlice = wgpu::kDepthSliceUndefined;
		attachment.loadOp = wgpu::LoadOp::Clear;
		attachment.storeOp = wgpu::StoreOp::Store;
		attachment.clearValue = { settings.ambient.r, settings.ambient.g, settings.ambient.b, 1.0 };
		wgpu::RenderPassDescriptor passDesc{};
		passDesc.colorAttachmentCount = 1;
		passDesc.colorAttachments = &attachment;
		auto pass{ encoder.BeginRenderPass(&passDesc) };
		if (!lights.empty())
		{
			const auto bytes{ lights.size_bytes() };
			ensure_buffer(device, m_lights, bytes, "Lights");
			queue.WriteBuffer(m_lights.buffer, 0, lights.data(), bytes);
			pass.SetPipeline(m_lightPipeline);
			pass.SetBindGroup(0, m_lights.bindGroup);
			pass.Draw(s_instance_vertices, static_cast<std::uint32_t>(lights.size()));
		}
		if (extraLights)
			extraLights(pass);
		pass.End();
	}

	{
		ZoneScopedN("LightRenderer::apply");
		wgpu::RenderPassColorAttachment attachment{};
		attachment.view = target;
		attachment.depthSlice = wgpu::kDepthSliceUndefined;
		attachment.loadOp = wgpu::LoadOp::Load;
		attachment.storeOp = wgpu::StoreOp::Store;
		wgpu::RenderPassDescriptor passDesc{};
		passDesc.colorAttachmentCount = 1;
		passDesc.colorAttachments = &attachment;
		auto pass{ encoder.BeginRenderPass(&passDesc) };
		pass.SetPipeline(settings.maxExposure > 1.0f ? m_brightApplyPipeline : m_applyPipeline);
		pass.SetBindGroup(0, m_applyBindGroup);
		pass.Draw(3);
		if (!glows.empty())
		{
			const auto bytes{ glows.size_bytes() };
			ensure_buffer(device, m_glows, bytes, "Glows");
			queue.WriteBuffer(m_glows.buffer, 0, glows.data(), bytes);
			pass.SetPipeline(m_glowPipeline);
			pass.SetBindGroup(0, m_glows.bindGroup);
			pass.Draw(s_instance_vertices, static_cast<std::uint32_t>(glows.size()));
		}
		pass.End();
	}

	m_stats = { static_cast<std::uint32_t>(lights.size()), static_cast<std::uint32_t>(glows.size()), mapWidth, mapHeight };
}
