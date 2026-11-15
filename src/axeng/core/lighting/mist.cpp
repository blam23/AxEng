#include "axeng/core/lighting/mist.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>

namespace
{
	// #embed paths are resolved relative to this file. The data is not null terminated.
	constexpr unsigned char s_mist_shader[] = {
#embed "mist.wgsl"
	};

	constexpr auto s_float_format{ wgpu::TextureFormat::RGBA16Float };
	constexpr auto s_mask_format{ wgpu::TextureFormat::R8Unorm };
	// The stirring grid is at most this many texels either way, and grows in blocks of this many.
	constexpr std::uint32_t s_max_grid{ 512 };
	constexpr std::uint32_t s_grid_block{ 64 };
	// World pixels the grid reaches past the view on every side.
	constexpr float s_grid_margin{ 96.0f };
	constexpr int s_jacobi_iterations{ 10 };
	// Halo texels are this many light map texels; the blur reaches at most this many steps either side.
	constexpr std::uint32_t s_halo_divisor{ 4 };
	constexpr float s_max_blur_steps{ 16.0f };
	// The tall mask (when the mist draws its own) is this many screen pixels a texel.
	constexpr std::uint32_t s_tall_divisor{ 4 };

	struct Uniforms
	{
		glm::vec4 view{}, frame{}, grid{}, gridSize{}, sim{}, noise{}, shape{}, wind{}, mask{}, maskSize{}, blur{},
			extent{}, blockers{}, blockerSize{};
	};
	static_assert(sizeof(Uniforms) == 224);

	struct CompositeUniforms
	{
		glm::vec4 colour{}, sun{}, shape{}, mask{}, density{}, grid{}, gridSize{};
	};
	static_assert(sizeof(CompositeUniforms) == 112);

	float finite_or(float value, float fallback) noexcept { return std::isfinite(value) ? value : fallback; }
}

bool ax::mist_visible(const Mist2D& mist) noexcept
{
	return std::isfinite(mist.amount) && mist.amount > 0.0f && mist.opacity > 0.0f;
}

ax::MistGrid ax::mist_grid(const LightView& view, float gridTexel) noexcept
{
	MistGrid grid{};
	if (!(view.zoom > 0.0f) || view.viewport.x <= 0.0f || view.viewport.y <= 0.0f)
		return grid;
	const glm::vec2 world{ view.viewport / view.zoom + glm::vec2{ s_grid_margin * 2.0f } };
	float texel{ std::clamp(finite_or(gridTexel, 8.0f), 1.0f, 256.0f) };
	while ((world.x / texel > static_cast<float>(s_max_grid - 1) || world.y / texel > static_cast<float>(s_max_grid - 1))
		&& texel < 65536.0f)
		texel *= 2.0f;
	const auto blocks{ [](float texels)
	{
		const auto needed{ static_cast<std::uint32_t>(std::ceil(texels)) + 1 };
		return std::min(s_max_grid, (needed + s_grid_block - 1) / s_grid_block * s_grid_block);
	} };
	grid.texel = texel;
	grid.width = blocks(world.x / texel);
	grid.height = blocks(world.y / texel);
	// Centred on the camera, snapped to whole texels so the state stays put in the world.
	const glm::vec2 size{ static_cast<float>(grid.width) * texel, static_cast<float>(grid.height) * texel };
	grid.origin = glm::floor((view.cameraPosition - size * 0.5f) / texel) * texel;
	return grid;
}

bool ax::MistRenderer::init(const wgpu::Device& device)
{
	if (m_densityPipeline)
		return true;
	wgpu::ShaderSourceWGSL source{};
	source.code = { reinterpret_cast<const char*>(s_mist_shader), sizeof(s_mist_shader) };
	wgpu::ShaderModuleDescriptor shaderDesc{};
	shaderDesc.nextInChain = &source;
	shaderDesc.label = "Mist shader";
	const auto shader{ device.CreateShaderModule(&shaderDesc) };

	std::array<wgpu::BindGroupLayoutEntry, 7> entries{};
	entries[0].binding = 0;
	entries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
	entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
	entries[0].buffer.minBindingSize = sizeof(Uniforms);
	for (const auto binding : { 1u, 2u, 3u })
	{
		entries[binding].binding = binding;
		entries[binding].visibility = wgpu::ShaderStage::Fragment;
		entries[binding].texture.sampleType = wgpu::TextureSampleType::Float;
		entries[binding].texture.viewDimension = wgpu::TextureViewDimension::e2D;
	}
	entries[4].binding = 4;
	entries[4].visibility = wgpu::ShaderStage::Fragment;
	entries[4].sampler.type = wgpu::SamplerBindingType::Filtering;
	entries[5].binding = 5;
	entries[5].visibility = wgpu::ShaderStage::Vertex;
	entries[5].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
	entries[6].binding = 6;
	entries[6].visibility = wgpu::ShaderStage::Fragment;
	entries[6].texture.sampleType = wgpu::TextureSampleType::Float;
	entries[6].texture.viewDimension = wgpu::TextureViewDimension::e2D;
	wgpu::BindGroupLayoutDescriptor layoutDesc{};
	layoutDesc.entryCount = entries.size();
	layoutDesc.entries = entries.data();
	m_layout = device.CreateBindGroupLayout(&layoutDesc);
	wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{};
	pipelineLayoutDesc.bindGroupLayoutCount = 1;
	pipelineLayoutDesc.bindGroupLayouts = &m_layout;
	const auto layout{ device.CreatePipelineLayout(&pipelineLayoutDesc) };

	const auto pipeline{ [&](const char* vertex, const char* fragment, wgpu::TextureFormat format,
		const wgpu::BlendState* blend)
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
		desc.vertex.entryPoint = vertex;
		desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
		desc.multisample.count = 1;
		desc.fragment = &fragmentState;
		return device.CreateRenderPipeline(&desc);
	} };
	wgpu::BlendState additive{};
	additive.color = { wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::One };
	additive.alpha = { wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::One };
	m_splatPipeline = pipeline("vs_mover", "fs_mover", s_float_format, &additive);
	m_advectPipeline = pipeline("vs_fullscreen", "fs_advect", s_float_format, nullptr);
	m_divergencePipeline = pipeline("vs_fullscreen", "fs_divergence", s_float_format, nullptr);
	m_jacobiPipeline = pipeline("vs_fullscreen", "fs_jacobi", s_float_format, nullptr);
	m_projectPipeline = pipeline("vs_fullscreen", "fs_project", s_float_format, nullptr);
	m_densityPipeline = pipeline("vs_fullscreen", "fs_density", s_float_format, nullptr);
	m_downsamplePipeline = pipeline("vs_fullscreen", "fs_downsample", s_float_format, nullptr);
	m_blurPipeline = pipeline("vs_fullscreen", "fs_blur", s_float_format, nullptr);

	// lighting.wgsl's fs_mist, group 1.
	std::array<wgpu::BindGroupLayoutEntry, 5> compositeEntries{};
	compositeEntries[0].binding = 0;
	compositeEntries[0].visibility = wgpu::ShaderStage::Fragment;
	compositeEntries[0].buffer.type = wgpu::BufferBindingType::Uniform;
	compositeEntries[0].buffer.minBindingSize = sizeof(CompositeUniforms);
	for (std::uint32_t binding{ 1 }; binding < 5; ++binding)
	{
		compositeEntries[binding].binding = binding;
		compositeEntries[binding].visibility = wgpu::ShaderStage::Fragment;
		compositeEntries[binding].texture.sampleType = wgpu::TextureSampleType::Float;
		compositeEntries[binding].texture.viewDimension = wgpu::TextureViewDimension::e2D;
	}
	wgpu::BindGroupLayoutDescriptor compositeLayoutDesc{};
	compositeLayoutDesc.entryCount = compositeEntries.size();
	compositeLayoutDesc.entries = compositeEntries.data();
	m_compositeLayout = device.CreateBindGroupLayout(&compositeLayoutDesc);

	wgpu::SamplerDescriptor samplerDesc{};
	samplerDesc.magFilter = wgpu::FilterMode::Linear;
	samplerDesc.minFilter = wgpu::FilterMode::Linear;
	samplerDesc.addressModeU = wgpu::AddressMode::ClampToEdge;
	samplerDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
	m_sampler = device.CreateSampler(&samplerDesc);
	for (auto& buffer : m_uniforms)
	{
		wgpu::BufferDescriptor desc{};
		desc.label = "Mist uniforms";
		desc.size = sizeof(Uniforms);
		desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
		buffer = gpu::create_buffer(device, desc, "Lighting");
	}
	{
		wgpu::BufferDescriptor desc{};
		desc.label = "Mist composite uniforms";
		desc.size = sizeof(CompositeUniforms);
		desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
		m_compositeUniforms = gpu::create_buffer(device, desc, "Lighting");
	}
	{
		// Stands in for every texture a pass does not use: zero.
		wgpu::TextureDescriptor desc{};
		desc.label = "Mist placeholder";
		desc.size = { 1, 1 };
		desc.format = s_float_format;
		desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
		m_dummy = gpu::create_texture(device, desc, "Lighting");
		constexpr std::array<std::uint16_t, 4> zero{};
		wgpu::TexelCopyTextureInfo destination{};
		destination.texture = m_dummy;
		wgpu::TexelCopyBufferLayout dataLayout{};
		dataLayout.bytesPerRow = sizeof(zero);
		dataLayout.rowsPerImage = 1;
		const wgpu::Extent3D size{ 1, 1, 1 };
		device.GetQueue().WriteTexture(&destination, zero.data(), sizeof(zero), &dataLayout, &size);
		m_dummyView = m_dummy.CreateView();
	}
	return m_splatPipeline && m_advectPipeline && m_divergencePipeline && m_jacobiPipeline && m_projectPipeline
		&& m_densityPipeline && m_downsamplePipeline && m_blurPipeline && m_compositeLayout;
}

ax::MistRenderer::Target ax::MistRenderer::make(const wgpu::Device& device, const char* label, std::uint32_t width,
	std::uint32_t height, wgpu::TextureFormat format) const
{
	wgpu::TextureDescriptor desc{};
	desc.label = label;
	desc.size = { std::max<std::uint32_t>(width, 1), std::max<std::uint32_t>(height, 1) };
	desc.format = format;
	desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopyDst;
	Target target{ gpu::create_texture(device, desc, "Lighting"), {}, desc.size.width, desc.size.height };
	if (target.texture)
		target.view = target.texture.CreateView();
	return target;
}

wgpu::BindGroup ax::MistRenderer::bind(const wgpu::Device& device, const gpu::TrackedBuffer& uniforms,
	const wgpu::TextureView& a, const wgpu::TextureView& b, const wgpu::TextureView& c) const
{
	std::array<wgpu::BindGroupEntry, 7> entries{};
	entries[0].binding = 0;
	entries[0].buffer = uniforms;
	entries[0].size = sizeof(Uniforms);
	entries[6].binding = 6;
	entries[6].textureView = m_blockers.target.view ? m_blockers.target.view : m_dummyView;
	entries[1].binding = 1;
	entries[1].textureView = a ? a : m_dummyView;
	entries[2].binding = 2;
	entries[2].textureView = b ? b : m_dummyView;
	entries[3].binding = 3;
	entries[3].textureView = c ? c : m_dummyView;
	entries[4].binding = 4;
	entries[4].sampler = m_sampler;
	entries[5].binding = 5;
	entries[5].buffer = m_movers;
	entries[5].size = m_moverCapacity;
	wgpu::BindGroupDescriptor desc{};
	desc.layout = m_layout;
	desc.entryCount = entries.size();
	desc.entries = entries.data();
	return device.CreateBindGroup(&desc);
}

bool ax::MistRenderer::upload(const wgpu::Device& device, const wgpu::Queue& queue, const MistMask* source,
	Uploaded& uploaded, const char* label)
{
	if (!source)
	{
		uploaded = {};
		return false;
	}
	const auto& mask{ *source };
	if (mask.version == uploaded.version && uploaded.target.texture)
		return true;
	uploaded.version = mask.version;
	const auto expected{ static_cast<std::size_t>(mask.width) * mask.height };
	if (mask.width == 0 || mask.height == 0 || mask.values.size() < expected)
	{
		uploaded.target = {};
		return false;
	}
	auto& target{ uploaded.target };
	if (!target.texture || target.width != mask.width || target.height != mask.height)
		target = make(device, label, mask.width, mask.height, s_mask_format);
	if (!target.texture)
		return false;
	// Rows of a texture copy need no alignment through the queue.
	wgpu::TexelCopyTextureInfo destination{};
	destination.texture = target.texture;
	wgpu::TexelCopyBufferLayout layout{};
	layout.bytesPerRow = mask.width;
	layout.rowsPerImage = mask.height;
	const wgpu::Extent3D size{ mask.width, mask.height, 1 };
	queue.WriteTexture(&destination, mask.values.data(), expected, &layout, &size);
	uploaded.rect = { mask.origin, std::max(mask.texel, 0.001f), 1.0f };
	return true;
}

bool ax::MistRenderer::encode(const wgpu::Device& device, const wgpu::Queue& queue, wgpu::CommandEncoder& encoder,
	const LightView& screen, const MistFrame& frame, const wgpu::TextureView& lightMap, std::uint32_t mapWidth,
	std::uint32_t mapHeight, std::uint32_t divisor, const TallMask& tall, const TallDraw& tallDraw, GpuPassTimer* timer)
{
	ZoneScopedN("MistRenderer::encode");
	m_stats = {};
	const auto& mist{ frame.mist };
	if (!m_densityPipeline || !mist_visible(mist) || screen.viewport.x <= 0.0f || screen.viewport.y <= 0.0f
		|| !(screen.zoom > 0.0f))
		return false;
	const auto beginPass{ [&](const wgpu::TextureView& target, const char* label, bool clear = true)
	{
		wgpu::RenderPassColorAttachment attachment{};
		attachment.view = target;
		attachment.depthSlice = wgpu::kDepthSliceUndefined;
		attachment.loadOp = clear ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
		attachment.storeOp = wgpu::StoreOp::Store;
		attachment.clearValue = { 0.0, 0.0, 0.0, 0.0 };
		wgpu::RenderPassDescriptor desc{};
		desc.label = label;
		desc.colorAttachmentCount = 1;
		desc.colorAttachments = &attachment;
		if (timer)
			desc.timestampWrites = timer->pass("GPU ms: mist");
		return encoder.BeginRenderPass(&desc);
	} };
	const auto fullscreen{ [&](const wgpu::RenderPipeline& pipeline, const wgpu::TextureView& target,
		const wgpu::BindGroup& group, const char* label)
	{
		auto pass{ beginPass(target, label) };
		pass.SetPipeline(pipeline);
		pass.SetBindGroup(0, group);
		pass.Draw(3);
		pass.End();
	} };

	// Movers, in a buffer that is always bound.
	const auto moverBytes{ std::max<std::size_t>(frame.movers.size_bytes(), MistMover::gpuDataSize) };
	if (!m_movers || m_moverCapacity < moverBytes)
	{
		m_moverCapacity = std::bit_ceil(std::max<std::size_t>(moverBytes, 4096));
		wgpu::BufferDescriptor desc{};
		desc.label = "Mist movers";
		desc.size = m_moverCapacity;
		desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
		m_movers = gpu::create_buffer(device, desc, "Lighting");
	}
	if (!frame.movers.empty())
		queue.WriteBuffer(m_movers, 0, frame.movers.data(), frame.movers.size_bytes());

	const bool masked{ upload(device, queue, frame.mask, m_mask, "Mist mask") };
	const bool blocked{ upload(device, queue, frame.blockers, m_blockers, "Mist blockers") };

	const auto dt{ std::clamp(finite_or(mist.deltaSeconds, 0.0f), 0.0f, 0.1f) };
	const auto resolution{ std::clamp(finite_or(mist.resolution, 2.0f), 1.0f, 8.0f) };
	const auto densityWidth{ std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::ceil(screen.viewport.x / resolution))) };
	const auto densityHeight{ std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::ceil(screen.viewport.y / resolution))) };

	// The stirring grid; a new size or texel starts it over.
	const bool stir{ mist.stir };
	const auto grid{ stir ? mist_grid(screen, mist.gridTexel) : MistGrid{} };
	const bool stirring{ stir && grid.width > 0 };
	if (stirring && (grid.width != m_state[0].width || grid.height != m_state[0].height || !m_state[0].texture))
	{
		for (auto& state : m_state) state = make(device, "Mist state", grid.width, grid.height, s_float_format);
		m_advected = make(device, "Mist advected", grid.width, grid.height, s_float_format);
		m_force = make(device, "Mist forces", grid.width, grid.height, s_float_format);
		m_divergence = make(device, "Mist divergence", grid.width, grid.height, s_float_format);
		for (auto& pressure : m_pressure) pressure = make(device, "Mist pressure", grid.width, grid.height, s_float_format);
		m_hasState = false;
	}
	if (stirring && grid.texel != m_grid.texel)
		m_hasState = false;
	if (!stirring)
		m_hasState = false;
	m_stats.reset = stirring && !m_hasState;

	const auto previousOrigin{ m_hasState ? m_grid.origin : grid.origin };
	Uniforms uniforms{
		.view = { screen.viewport, screen.cameraPosition },
		.frame = { screen.zoom, resolution, finite_or(mist.time, 0.0f), dt },
		.grid = { grid.origin, grid.texel, m_hasState ? 1.0f : 0.0f },
		.gridSize = { static_cast<float>(grid.width), static_cast<float>(grid.height), previousOrigin },
		.sim = { 1.0f / std::max(finite_or(mist.refillSeconds, 4.0f), 0.05f), std::max(finite_or(mist.damping, 1.0f), 0.0f),
			std::max(finite_or(mist.displace, 0.0f), 0.0f), stirring ? 1.0f : 0.0f },
		.noise = { std::max(finite_or(mist.scale, 384.0f), 1.0f), finite_or(mist.warp, 0.0f), finite_or(mist.seed, 0.0f), 0.0f },
		.shape = { std::clamp(mist.amount, 0.0f, 1.0f), finite_or(mist.coverage, 0.5f), finite_or(mist.softness, 0.2f),
			masked ? finite_or(mist.maskBias, 0.0f) : 0.0f },
		.wind = { finite_or(mist.wind.x, 0.0f), finite_or(mist.wind.y, 0.0f), 0.0f, 0.0f },
		.mask = masked ? m_mask.rect : glm::vec4{},
		.maskSize = { static_cast<float>(m_mask.target.width), static_cast<float>(m_mask.target.height), 0.0f, 0.0f },
		.extent = { static_cast<float>(grid.width), static_cast<float>(grid.height), 0.0f, 0.0f },
		.blockers = blocked ? m_blockers.rect : glm::vec4{},
		.blockerSize = { static_cast<float>(m_blockers.target.width), static_cast<float>(m_blockers.target.height),
			std::clamp(finite_or(mist.blocking, 1.0f), 0.0f, 1.0f), 0.0f },
	};

	if (stirring)
	{
		queue.WriteBuffer(m_uniforms[0], 0, &uniforms, sizeof(uniforms));
		const auto& previous{ m_state[m_current] };
		auto& next{ m_state[1 - m_current] };
		{
			auto pass{ beginPass(m_force.view, "Mist forces") };
			if (!frame.movers.empty())
			{
				pass.SetPipeline(m_splatPipeline);
				pass.SetBindGroup(0, bind(device, m_uniforms[0], {}, {}, {}));
				pass.Draw(6, static_cast<std::uint32_t>(frame.movers.size()));
			}
			pass.End();
		}
		fullscreen(m_advectPipeline, m_advected.view, bind(device, m_uniforms[0], previous.view, m_force.view, {}),
			"Mist advect");
		fullscreen(m_divergencePipeline, m_divergence.view, bind(device, m_uniforms[0], m_advected.view, {}, {}),
			"Mist divergence");
		beginPass(m_pressure[0].view, "Mist pressure clear").End();
		std::size_t pressure{};
		for (int iteration{}; iteration < s_jacobi_iterations; ++iteration)
		{
			fullscreen(m_jacobiPipeline, m_pressure[1 - pressure].view,
				bind(device, m_uniforms[0], m_pressure[pressure].view, m_divergence.view, {}), "Mist pressure");
			pressure = 1 - pressure;
		}
		fullscreen(m_projectPipeline, next.view, bind(device, m_uniforms[0], m_advected.view, m_pressure[pressure].view, {}),
			"Mist project");
		m_current = 1 - m_current;
		m_hasState = true;
		m_grid = grid;
	}
	else
		m_grid = {};

	// The density over the screen.
	if (!m_density.texture || m_density.width != densityWidth || m_density.height != densityHeight)
		m_density = make(device, "Mist density", densityWidth, densityHeight, s_float_format);
	{
		auto densityUniforms{ uniforms };
		densityUniforms.extent = { static_cast<float>(densityWidth), static_cast<float>(densityHeight), 0.0f, 0.0f };
		queue.WriteBuffer(m_uniforms[1], 0, &densityUniforms, sizeof(densityUniforms));
		fullscreen(m_densityPipeline, m_density.view, bind(device, m_uniforms[1],
			stirring ? m_state[m_current].view : wgpu::TextureView{}, {}, masked ? m_mask.target.view : wgpu::TextureView{}),
			"Mist density");
	}

	// Halos: the light map, downsampled and blurred.
	const bool halos{ mist.haloStrength > 0.0f && mist.haloRadius > 0.0f && lightMap };
	if (halos)
	{
		const auto haloWidth{ std::max<std::uint32_t>(1, (mapWidth + s_halo_divisor - 1) / s_halo_divisor) };
		const auto haloHeight{ std::max<std::uint32_t>(1, (mapHeight + s_halo_divisor - 1) / s_halo_divisor) };
		if (!m_halo[0].texture || m_halo[0].width != haloWidth || m_halo[0].height != haloHeight)
			for (auto& halo : m_halo) halo = make(device, "Mist halo", haloWidth, haloHeight, s_float_format);
		const auto haloPixels{ static_cast<float>(s_halo_divisor * std::max<std::uint32_t>(divisor, 1)) };
		const auto sigma{ std::max(mist.haloRadius * 0.5f * screen.zoom / haloPixels, 0.001f) };
		const auto reach{ sigma * 2.5f };
		const auto step{ std::max(1.0f, reach / s_max_blur_steps) };
		const auto steps{ std::ceil(reach / step) };
		Uniforms blur{ uniforms };
		blur.extent = { static_cast<float>(haloWidth), static_cast<float>(haloHeight), 0.0f, 0.0f };
		auto across{ blur };
		across.blur = { step, 0.0f, sigma / step, steps };
		auto down{ blur };
		down.blur = { 0.0f, step, sigma / step, steps };
		queue.WriteBuffer(m_uniforms[2], 0, &across, sizeof(across));
		fullscreen(m_downsamplePipeline, m_halo[0].view, bind(device, m_uniforms[2], lightMap, {}, {}), "Mist halo");
		fullscreen(m_blurPipeline, m_halo[1].view, bind(device, m_uniforms[2], m_halo[0].view, {}, {}),
			"Mist halo across");
		// Queue writes all land before the frame's commands run, so each set of uniforms has a buffer of its own.
		queue.WriteBuffer(m_uniforms[3], 0, &down, sizeof(down));
		fullscreen(m_blurPipeline, m_halo[0].view, bind(device, m_uniforms[3], m_halo[1].view, {}, {}),
			"Mist halo down");
	}

	// The tall things' silhouettes: the ambient occlusion's, or drawn here.
	wgpu::TextureView tallView{ tall.view };
	float tallMargin{ tall.margin };
	glm::vec2 tallCovered{ screen.viewport + glm::vec2{ tall.margin * 2.0f } };
	if (!tallView && mist.tallThinning > 0.0f && tallDraw)
	{
		const auto width{ std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::ceil(screen.viewport.x / s_tall_divisor))) };
		const auto height{ std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::ceil(screen.viewport.y / s_tall_divisor))) };
		if (!m_tall.texture || m_tall.width != width || m_tall.height != height)
			m_tall = make(device, "Mist tall mask", width, height, s_mask_format);
		auto pass{ beginPass(m_tall.view, "Mist tall mask") };
		tallDraw(pass, screen);
		pass.End();
		tallView = m_tall.view;
		tallMargin = 0.0f;
		tallCovered = screen.viewport;
	}

	const CompositeUniforms composite{
		.colour = { glm::max(mist.colour, glm::vec3{ 0.0f }), std::clamp(mist.opacity, 0.0f, 1.0f) },
		.sun = { glm::length(mist.toSun) > 0.0f ? glm::normalize(mist.toSun) : glm::vec3{ 0.0f, 0.0f, 1.0f },
			std::max(finite_or(mist.sunStrength, 0.0f), 0.0f) },
		.shape = { halos ? std::max(mist.haloStrength, 0.0f) : 0.0f, std::clamp(mist.tallThinning, 0.0f, 1.0f),
			static_cast<float>(mist.debugView), tallView ? 1.0f : 0.0f },
		.mask = { tallMargin, tallCovered, 0.0f },
		.density = { static_cast<float>(densityWidth), static_cast<float>(densityHeight), resolution, 0.0f },
		.grid = { m_grid.origin, m_grid.texel, stirring ? 1.0f : 0.0f },
		.gridSize = { static_cast<float>(m_grid.width), static_cast<float>(m_grid.height), 0.0f, 0.0f },
	};
	queue.WriteBuffer(m_compositeUniforms, 0, &composite, sizeof(composite));
	std::array<wgpu::BindGroupEntry, 5> entries{};
	entries[0].binding = 0;
	entries[0].buffer = m_compositeUniforms;
	entries[0].size = sizeof(CompositeUniforms);
	entries[1].binding = 1;
	entries[1].textureView = m_density.view;
	entries[2].binding = 2;
	entries[2].textureView = halos ? m_halo[0].view : m_dummyView;
	entries[3].binding = 3;
	entries[3].textureView = tallView ? tallView : m_dummyView;
	entries[4].binding = 4;
	entries[4].textureView = stirring ? m_state[m_current].view : m_dummyView;
	wgpu::BindGroupDescriptor desc{};
	desc.layout = m_compositeLayout;
	desc.entryCount = entries.size();
	desc.entries = entries.data();
	m_compositeGroup = device.CreateBindGroup(&desc);

	m_stats.gridWidth = stirring ? grid.width : 0;
	m_stats.gridHeight = stirring ? grid.height : 0;
	m_stats.movers = static_cast<std::uint32_t>(frame.movers.size());
	return m_density.view != nullptr;
}
