#include "axeng/core/materials/backdrop_blur.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace
{
	constexpr const char* s_downsample = R"WGSL(
@group(0) @binding(0) var source : texture_2d<f32>;
@group(0) @binding(1) var source_sampler : sampler;

struct VSOut {
	@builtin(position) pos : vec4<f32>,
	@location(0) uv : vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) index : u32) -> VSOut {
	let p = vec2<f32>(f32((index << 1u) & 2u), f32(index & 2u));
	var out : VSOut;
	out.pos = vec4<f32>(p * 2.0 - 1.0, 0.0, 1.0);
	out.uv = vec2<f32>(p.x, 1.0 - p.y);
	return out;
}

fn tap(uv : vec2<f32>, offset : vec2<f32>, texel : vec2<f32>) -> vec3<f32> {
	return textureSampleLevel(source, source_sampler, uv + offset * texel, 0.0).rgb;
}

@fragment
fn fs_main(in_ : VSOut) -> @location(0) vec4<f32> {
	let texel = 1.0 / vec2<f32>(textureDimensions(source));
	let uv = in_.uv;
	let a = tap(uv, vec2<f32>(-2.0, 2.0), texel);
	let b = tap(uv, vec2<f32>(0.0, 2.0), texel);
	let c = tap(uv, vec2<f32>(2.0, 2.0), texel);
	let d = tap(uv, vec2<f32>(-2.0, 0.0), texel);
	let e = tap(uv, vec2<f32>(0.0, 0.0), texel);
	let f = tap(uv, vec2<f32>(2.0, 0.0), texel);
	let g = tap(uv, vec2<f32>(-2.0, -2.0), texel);
	let h = tap(uv, vec2<f32>(0.0, -2.0), texel);
	let i = tap(uv, vec2<f32>(2.0, -2.0), texel);
	let j = tap(uv, vec2<f32>(-1.0, 1.0), texel);
	let k = tap(uv, vec2<f32>(1.0, 1.0), texel);
	let l = tap(uv, vec2<f32>(-1.0, -1.0), texel);
	let m = tap(uv, vec2<f32>(1.0, -1.0), texel);
	let colour = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
	return vec4<f32>(colour, 1.0);
}
)WGSL";
}

bool ax::BackdropBlur::init(const wgpu::Device& device, wgpu::TextureFormat format, std::uint32_t levels)
{
	if (m_device.Get() == device.Get() && m_format == format && m_levels == levels && m_pipeline != nullptr)
		return true;
	*this = {};
	m_device = device;
	m_format = format;
	m_levels = levels;

	wgpu::BindGroupLayoutEntry entries[2]{};
	entries[0].binding = 0;
	entries[0].visibility = wgpu::ShaderStage::Fragment;
	entries[0].texture.sampleType = wgpu::TextureSampleType::Float;
	entries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;
	entries[1].binding = 1;
	entries[1].visibility = wgpu::ShaderStage::Fragment;
	entries[1].sampler.type = wgpu::SamplerBindingType::Filtering;
	wgpu::BindGroupLayoutDescriptor layoutDesc{};
	layoutDesc.entryCount = 2;
	layoutDesc.entries = entries;
	m_layout = device.CreateBindGroupLayout(&layoutDesc);

	wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{};
	pipelineLayoutDesc.bindGroupLayoutCount = 1;
	pipelineLayoutDesc.bindGroupLayouts = &m_layout;
	const auto pipelineLayout{ device.CreatePipelineLayout(&pipelineLayoutDesc) };

	wgpu::ShaderSourceWGSL wgsl{};
	wgsl.code = { s_downsample, std::strlen(s_downsample) };
	wgpu::ShaderModuleDescriptor shaderDesc{};
	shaderDesc.nextInChain = &wgsl;
	shaderDesc.label = "BackdropBlur";
	const auto shader{ device.CreateShaderModule(&shaderDesc) };

	wgpu::ColorTargetState target{};
	target.format = format;
	target.writeMask = wgpu::ColorWriteMask::All;
	wgpu::FragmentState fragment{};
	fragment.module = shader;
	fragment.entryPoint = "fs_main";
	fragment.targetCount = 1;
	fragment.targets = &target;
	wgpu::RenderPipelineDescriptor pipelineDesc{};
	pipelineDesc.label = "BackdropBlur";
	pipelineDesc.layout = pipelineLayout;
	pipelineDesc.vertex.module = shader;
	pipelineDesc.vertex.entryPoint = "vs_main";
	pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
	pipelineDesc.primitive.cullMode = wgpu::CullMode::None;
	pipelineDesc.multisample.count = 1;
	pipelineDesc.multisample.mask = ~0u;
	pipelineDesc.fragment = &fragment;
	m_pipeline = device.CreateRenderPipeline(&pipelineDesc);

	wgpu::SamplerDescriptor samplerDesc{};
	samplerDesc.addressModeU = samplerDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
	samplerDesc.magFilter = samplerDesc.minFilter = wgpu::FilterMode::Linear;
	samplerDesc.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
	samplerDesc.maxAnisotropy = 1;
	m_sampler = device.CreateSampler(&samplerDesc);
	return true;
}

bool ax::BackdropBlur::resize(std::uint32_t width, std::uint32_t height)
{
	if (m_texture != nullptr && m_width == width && m_height == height)
		return true;
	m_texture = {};
	m_chainView = {};
	m_levelViews.clear();
	m_downsampleGroups.clear();
	if (width == 0 || height == 0)
		return false;

	const auto fits{ static_cast<std::uint32_t>(std::bit_width(std::min(width, height))) };
	const auto mips{ std::min(m_levels + 1, fits) };
	wgpu::TextureDescriptor desc{};
	desc.label = "BackdropBlur";
	desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
	desc.dimension = wgpu::TextureDimension::e2D;
	desc.size = { width, height, 1 };
	desc.format = m_format;
	desc.mipLevelCount = mips;
	desc.sampleCount = 1;
	m_texture = m_device.CreateTexture(&desc);
	if (m_texture == nullptr)
		return false;
	m_chainView = m_texture.CreateView();

	for (std::uint32_t level = 0; level < mips; ++level)
	{
		wgpu::TextureViewDescriptor view{};
		view.baseMipLevel = level;
		view.mipLevelCount = 1;
		view.dimension = wgpu::TextureViewDimension::e2D;
		m_levelViews.push_back(m_texture.CreateView(&view));
	}
	for (std::uint32_t level = 1; level < mips; ++level)
	{
		wgpu::BindGroupEntry bindings[2]{};
		bindings[0].binding = 0;
		bindings[0].textureView = m_levelViews[level - 1];
		bindings[1].binding = 1;
		bindings[1].sampler = m_sampler;
		wgpu::BindGroupDescriptor groupDesc{};
		groupDesc.layout = m_layout;
		groupDesc.entryCount = 2;
		groupDesc.entries = bindings;
		m_downsampleGroups.push_back(m_device.CreateBindGroup(&groupDesc));
	}
	m_width = width;
	m_height = height;
	return true;
}

bool ax::BackdropBlur::encode(const wgpu::CommandEncoder& encoder, const wgpu::TextureView& multisampled,
	const wgpu::Texture& frame, std::uint32_t width, std::uint32_t height)
{
	if (m_pipeline == nullptr || !resize(width, height))
		return false;
	if (multisampled != nullptr)
	{
		// An empty pass whose only job is to resolve; the multisampled frame is kept for the interface.
		wgpu::RenderPassColorAttachment attachment{};
		attachment.view = multisampled;
		attachment.depthSlice = wgpu::kDepthSliceUndefined;
		attachment.resolveTarget = m_levelViews[0];
		attachment.loadOp = wgpu::LoadOp::Load;
		attachment.storeOp = wgpu::StoreOp::Store;
		wgpu::RenderPassDescriptor passDesc{};
		passDesc.label = "BackdropBlur resolve";
		passDesc.colorAttachmentCount = 1;
		passDesc.colorAttachments = &attachment;
		encoder.BeginRenderPass(&passDesc).End();
	}
	else if (frame != nullptr)
	{
		wgpu::TexelCopyTextureInfo source{};
		source.texture = frame;
		wgpu::TexelCopyTextureInfo destination{};
		destination.texture = m_texture;
		const wgpu::Extent3D extent{ width, height, 1 };
		encoder.CopyTextureToTexture(&source, &destination, &extent);
	}
	else
		return false;

	for (std::uint32_t level = 1; level < m_levelViews.size(); ++level)
	{
		wgpu::RenderPassColorAttachment attachment{};
		attachment.view = m_levelViews[level];
		attachment.depthSlice = wgpu::kDepthSliceUndefined;
		attachment.loadOp = wgpu::LoadOp::Clear;
		attachment.storeOp = wgpu::StoreOp::Store;
		attachment.clearValue = { 0.0, 0.0, 0.0, 1.0 };
		wgpu::RenderPassDescriptor passDesc{};
		passDesc.label = "BackdropBlur downsample";
		passDesc.colorAttachmentCount = 1;
		passDesc.colorAttachments = &attachment;
		auto pass{ encoder.BeginRenderPass(&passDesc) };
		pass.SetPipeline(m_pipeline);
		pass.SetBindGroup(0, m_downsampleGroups[level - 1]);
		pass.Draw(3);
		pass.End();
	}
	return true;
}
