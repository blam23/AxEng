#include "axeng/core/lighting/radiance_cascades.h"
#include "axeng/core/lighting/light_renderer.h"

#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>

namespace
{
	// #embed paths are resolved relative to this file. The data is not null terminated.
	constexpr unsigned char s_radiance_shader[] = {
#embed "radiance.wgsl"
	};

	constexpr auto s_jump_format{ wgpu::TextureFormat::RG16Uint };
	constexpr auto s_distance_format{ wgpu::TextureFormat::R32Uint };
	constexpr auto s_cascade_format{ wgpu::TextureFormat::RGBA16Float };
	constexpr std::uint32_t s_max_cascades{ 7 };
	constexpr std::uint32_t s_max_texels{ 4096 };
	// Uniform slots, one per pass, at the minimum dynamic offset alignment.
	constexpr std::uint32_t s_slot_stride{ 256 };
	constexpr std::uint32_t s_slots{ 32 };
	// Cascade 0's interval, in probe spacings.
	constexpr float s_interval0{ 0.5f };

	struct Uniforms
	{
		glm::vec4 grid{};
		glm::vec4 probes{};
		glm::vec4 cascade{};
		glm::vec4 params{};
		glm::vec4 ambient{};
		glm::vec4 view{};
		glm::vec4 viewport{};
		glm::vec4 previous{};
	};
	static_assert(sizeof(Uniforms) == 128);

	// The interval reach of n cascades, in texels: interval0 * (4^n - 1) / 3.
	float reach_of(std::uint32_t cascades, float interval0) noexcept
	{
		return interval0 * (std::ldexp(1.0f, static_cast<int>(2 * cascades)) - 1.0f) / 3.0f;
	}

	std::uint32_t cascades_for(float reachTexels, float interval0) noexcept
	{
		std::uint32_t count{ 2 };
		while (count < s_max_cascades && reach_of(count, interval0) < reachTexels)
			++count;
		return count;
	}
}

ax::LightView ax::RadianceGrid::view() const noexcept
{
	const glm::vec2 size{ static_cast<float>(width), static_cast<float>(height) };
	return { .viewport = size, .cameraPosition = origin + size * 0.5f / texelsPerPixel, .zoom = texelsPerPixel };
}

ax::RadianceGrid ax::radiance_grid(const LightView& view, const RadianceSettings& settings, float reach) noexcept
{
	RadianceGrid grid{};
	if (view.viewport.x <= 0.0f || view.viewport.y <= 0.0f || view.zoom <= 0.0f)
		return grid;
	const auto divisor{ static_cast<float>(std::clamp<std::uint32_t>(settings.divisor, 1, 8)) };
	grid.probeSpacing = std::clamp<std::uint32_t>(settings.probeSpacing, 1, 2);
	grid.texelsPerPixel = view.zoom / divisor;
	const auto halfView{ view.viewport * 0.5f / view.zoom };
	const auto margin{ std::clamp(settings.margin, 0.0f,
		std::max(halfView.x, halfView.y) * 2.0f * std::max(settings.maxMarginFraction, 0.0f)) };
	auto low{ view.cameraPosition - halfView - margin };
	auto high{ view.cameraPosition + halfView + margin };
	// Too many texels: coarsen the grid rather than drop the margin.
	const auto texels{ (high - low) * grid.texelsPerPixel };
	const auto over{ std::max(texels.x, texels.y) / static_cast<float>(s_max_texels - 128) };
	if (over > 1.0f)
		grid.texelsPerPixel /= over;

	const auto interval0{ s_interval0 * static_cast<float>(grid.probeSpacing) };
	grid.cascades = cascades_for(std::max(reach, 1.0f) * grid.texelsPerPixel, interval0);
	// Snapped to the coarsest probe spacing, in texels and in world pixels.
	const auto snap{ grid.probeSpacing << (grid.cascades - 1) };
	const auto snapWorld{ static_cast<float>(snap) / grid.texelsPerPixel };
	grid.origin = glm::floor(low / snapWorld) * snapWorld;
	const auto extent{ (high - grid.origin) * grid.texelsPerPixel };
	const auto padded{ [&](float texelsNeeded)
	{
		return std::max<std::uint32_t>(snap, static_cast<std::uint32_t>(std::ceil(texelsNeeded / static_cast<float>(snap))) * snap);
	} };
	grid.width = std::min(padded(extent.x), s_max_texels / snap * snap);
	grid.height = std::min(padded(extent.y), s_max_texels / snap * snap);
	return grid;
}

bool ax::radiance_traces(const Light2D& light, const RadianceGrid& grid, const RadianceSettings& settings) noexcept
{
	if (!settings.enabled || grid.width == 0 || light.shadows <= 0.0f || light.length > 0.0f || light.intensity <= 0.0f
		|| light.radius * grid.texelsPerPixel < settings.minLightTexels)
		return false;
	const auto inset{ std::max(light.sourceRadius, 2.5f * static_cast<float>(grid.probeSpacing) / grid.texelsPerPixel) };
	const auto low{ grid.world_min() + inset };
	const auto high{ grid.world_max() - inset };
	return light.position.x >= low.x && light.position.y >= low.y && light.position.x <= high.x && light.position.y <= high.y;
}

bool ax::RadianceCascades::init(const wgpu::Device& device, wgpu::TextureFormat lightMapFormat)
{
	if (m_cascadePipeline && m_device.Get() == device.Get() && m_lightMapFormat == lightMapFormat)
		return true;
	m_device = device;
	m_lightMapFormat = lightMapFormat;
	m_width = m_height = m_probeSpacing = 0;
	m_ready = false;

	wgpu::ShaderSourceWGSL source{};
	source.code = { reinterpret_cast<const char*>(s_radiance_shader), sizeof(s_radiance_shader) };
	wgpu::ShaderModuleDescriptor shaderDesc{};
	shaderDesc.nextInChain = &source;
	shaderDesc.label = "Radiance cascades shader";
	const auto shader{ device.CreateShaderModule(&shaderDesc) };

	std::array<wgpu::BindGroupLayoutEntry, 8> entries{};
	const auto stages{ wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment };
	entries[0].binding = 0;
	entries[0].visibility = stages;
	entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
	entries[0].buffer.hasDynamicOffset = true;
	entries[0].buffer.minBindingSize = sizeof(Uniforms);
	for (const auto binding : { 1u, 2u, 4u, 7u })
	{
		entries[binding].binding = binding;
		entries[binding].visibility = wgpu::ShaderStage::Fragment;
		entries[binding].texture.sampleType = wgpu::TextureSampleType::Float;
		entries[binding].texture.viewDimension = wgpu::TextureViewDimension::e2D;
	}
	entries[3].binding = 3;
	entries[3].visibility = wgpu::ShaderStage::Fragment;
	entries[3].texture.sampleType = wgpu::TextureSampleType::Uint;
	entries[3].texture.viewDimension = wgpu::TextureViewDimension::e2D;
	entries[5].binding = 5;
	entries[5].visibility = wgpu::ShaderStage::Fragment;
	entries[5].sampler.type = wgpu::SamplerBindingType::Filtering;
	entries[6].binding = 6;
	entries[6].visibility = stages;
	entries[6].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
	wgpu::BindGroupLayoutDescriptor layoutDesc{};
	layoutDesc.entryCount = entries.size();
	layoutDesc.entries = entries.data();
	m_layout = device.CreateBindGroupLayout(&layoutDesc);
	wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{};
	pipelineLayoutDesc.bindGroupLayoutCount = 1;
	pipelineLayoutDesc.bindGroupLayouts = &m_layout;
	const auto pipelineLayout{ device.CreatePipelineLayout(&pipelineLayoutDesc) };

	const auto pipeline{ [&](const char* vertex, const char* fragment, std::initializer_list<wgpu::TextureFormat> formats)
	{
		std::array<wgpu::ColorTargetState, 2> targets{};
		std::uint32_t count{};
		for (const auto format : formats)
		{
			targets[count].format = format;
			targets[count].writeMask = wgpu::ColorWriteMask::All;
			++count;
		}
		wgpu::FragmentState fragmentState{};
		fragmentState.module = shader;
		fragmentState.entryPoint = fragment;
		fragmentState.targetCount = count;
		fragmentState.targets = targets.data();
		wgpu::RenderPipelineDescriptor desc{};
		desc.label = fragment;
		desc.layout = pipelineLayout;
		desc.vertex.module = shader;
		desc.vertex.entryPoint = vertex;
		desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
		desc.primitive.cullMode = wgpu::CullMode::None;
		desc.multisample.count = 1;
		desc.multisample.mask = ~0u;
		desc.fragment = &fragmentState;
		return device.CreateRenderPipeline(&desc);
	} };
	m_circlePipeline = pipeline("vs_circle", "fs_instance", { radiance_scene_format });
	m_emitterPipeline = pipeline("vs_emitter", "fs_instance", { radiance_scene_format });
	m_seedPipeline = pipeline("vs_full", "fs_seed", { s_jump_format });
	m_jumpPipeline = pipeline("vs_full", "fs_jump", { s_jump_format });
	m_distancePipeline = pipeline("vs_full", "fs_distance", { s_distance_format });
	m_resolvePipeline = pipeline("vs_full", "fs_resolve", { lightMapFormat });
	m_cascadePipeline = pipeline("vs_full", "fs_cascade", { s_cascade_format });

	wgpu::BufferDescriptor uniformDesc{};
	uniformDesc.size = s_slot_stride * s_slots;
	uniformDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform;
	uniformDesc.label = "Radiance cascades uniforms";
	m_uniforms = gpu::create_buffer(device, uniformDesc, "Lighting");
	m_lights = {};
	m_circles = {};
	m_lightCapacity = m_circleCapacity = 0;
	ensure_storage(device, m_lights, m_lightCapacity, 64, "Radiance lights");
	ensure_storage(device, m_circles, m_circleCapacity, 64, "Radiance circles");

	wgpu::SamplerDescriptor samplerDesc{};
	samplerDesc.addressModeU = wgpu::AddressMode::ClampToEdge;
	samplerDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
	samplerDesc.magFilter = wgpu::FilterMode::Linear;
	samplerDesc.minFilter = wgpu::FilterMode::Linear;
	m_sampler = device.CreateSampler(&samplerDesc);

	const auto dummy{ [&](wgpu::TextureFormat format, const char* label)
	{
		wgpu::TextureDescriptor desc{};
		desc.label = label;
		desc.size = { 1, 1 };
		desc.format = format;
		desc.usage = wgpu::TextureUsage::TextureBinding;
		Target target{ gpu::create_texture(device, desc, "Lighting"), {} };
		target.view = target.texture.CreateView();
		return target;
	} };
	m_dummyFloat = dummy(wgpu::TextureFormat::RGBA16Float, "Radiance dummy float");
	m_dummyUint = dummy(wgpu::TextureFormat::R32Uint, "Radiance dummy uint");

	if (!m_circlePipeline || !m_emitterPipeline || !m_seedPipeline || !m_jumpPipeline || !m_distancePipeline
		|| !m_resolvePipeline || !m_cascadePipeline)
	{
		spdlog::error("Failed to create the radiance cascades pipelines.");
		m_cascadePipeline = {};
		return false;
	}
	return true;
}

void ax::RadianceCascades::ensure_storage(const wgpu::Device& device, gpu::TrackedBuffer& buffer, std::size_t& capacity,
	std::size_t bytes, const char* label)
{
	if (buffer && capacity >= bytes)
		return;
	capacity = std::bit_ceil(std::max<std::size_t>(bytes, 4096));
	wgpu::BufferDescriptor desc{};
	desc.size = capacity;
	desc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Storage;
	desc.label = label;
	buffer = gpu::create_buffer(device, desc, "Lighting");
}

bool ax::RadianceCascades::ensure_targets(const wgpu::Device& device, std::uint32_t width, std::uint32_t height,
	std::uint32_t probeSpacing)
{
	if (m_width == width && m_height == height && m_probeSpacing == probeSpacing && m_scene.texture)
		return true;
	const auto make{ [&](std::uint32_t w, std::uint32_t h, wgpu::TextureFormat format, const char* label)
	{
		wgpu::TextureDescriptor desc{};
		desc.label = label;
		desc.size = { w, h };
		desc.format = format;
		desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment;
		Target target{ gpu::create_texture(device, desc, "Lighting"), {} };
		if (target.texture)
			target.view = target.texture.CreateView();
		return target;
	} };
	m_scene = make(width, height, radiance_scene_format, "Radiance scene");
	m_jump[0] = make(width, height, s_jump_format, "Radiance jump flood A");
	m_jump[1] = make(width, height, s_jump_format, "Radiance jump flood B");
	m_distance = make(width, height, s_distance_format, "Radiance distance");
	m_cascade[0] = make(width / probeSpacing, height / probeSpacing, s_cascade_format, "Radiance cascade A");
	m_cascade[1] = make(width / probeSpacing, height / probeSpacing, s_cascade_format, "Radiance cascade B");
	const bool ok{ m_scene.view && m_jump[0].view && m_jump[1].view && m_distance.view
		&& m_cascade[0].view && m_cascade[1].view };
	m_width = ok ? width : 0;
	m_height = ok ? height : 0;
	m_probeSpacing = ok ? probeSpacing : 0;
	return ok;
}

wgpu::BindGroup ax::RadianceCascades::bind(const wgpu::Device& device, const wgpu::TextureView& a,
	const wgpu::TextureView& b, const wgpu::TextureView& uintTexture, const wgpu::TextureView& filtered,
	const wgpu::TextureView& e, const wgpu::Buffer& storage) const
{
	const auto orFloat{ [this](const wgpu::TextureView& view) { return view ? view : m_dummyFloat.view; } };
	std::array<wgpu::BindGroupEntry, 8> entries{};
	entries[0].binding = 0;
	entries[0].buffer = m_uniforms;
	entries[0].size = sizeof(Uniforms);
	entries[1].binding = 1;
	entries[1].textureView = orFloat(a);
	entries[2].binding = 2;
	entries[2].textureView = orFloat(b);
	entries[3].binding = 3;
	entries[3].textureView = uintTexture ? uintTexture : m_dummyUint.view;
	entries[4].binding = 4;
	entries[4].textureView = orFloat(filtered);
	entries[5].binding = 5;
	entries[5].sampler = m_sampler;
	entries[6].binding = 6;
	entries[6].buffer = storage ? storage : wgpu::Buffer{ m_lights };
	entries[7].binding = 7;
	entries[7].textureView = orFloat(e);
	wgpu::BindGroupDescriptor desc{};
	desc.layout = m_layout;
	desc.entryCount = entries.size();
	desc.entries = entries.data();
	return device.CreateBindGroup(&desc);
}

void ax::RadianceCascades::encode(const wgpu::Device& device, const wgpu::Queue& queue, wgpu::CommandEncoder& encoder,
	const RadianceGrid& grid, const RadianceSettings& settings, const LightView& view, std::uint32_t lightMapDivisor,
	glm::vec3 ambient, std::span<const Light2D> lights, std::span<const RadianceCircle> circles,
	const OccluderDraw& occluders, const Previous& previous)
{
	ZoneScopedN("RadianceCascades::encode");
	m_ready = false;
	m_stats = {};
	if (!m_cascadePipeline || grid.width == 0 || grid.height == 0
		|| !ensure_targets(device, grid.width, grid.height, grid.probeSpacing))
		return;

	// Lights: three vec4s each.
	m_lightScratch.clear();
	for (const auto& light : lights)
	{
		const auto colour{ light.colour * light.intensity };
		m_lightScratch.insert(m_lightScratch.end(), { light.position.x, light.position.y, light.radius, light.sourceRadius,
			colour.r, colour.g, colour.b, light.falloff,
			light.direction.x, light.direction.y, light.coneInner, light.coneOuter });
	}
	if (!m_lightScratch.empty())
	{
		ensure_storage(device, m_lights, m_lightCapacity, m_lightScratch.size() * sizeof(float), "Radiance lights");
		queue.WriteBuffer(m_lights, 0, m_lightScratch.data(), m_lightScratch.size() * sizeof(float));
	}
	if (!circles.empty())
	{
		ensure_storage(device, m_circles, m_circleCapacity, circles.size_bytes(), "Radiance circles");
		queue.WriteBuffer(m_circles, 0, circles.data(), circles.size_bytes());
	}

	const auto width{ static_cast<float>(grid.width) };
	const auto height{ static_cast<float>(grid.height) };
	const auto spacing{ static_cast<float>(grid.probeSpacing) };
	const auto interval0{ s_interval0 * spacing };
	// The last cascade whose interval ends within the sky occlusion range keeps its visibility.
	const auto aoTexels{ settings.aoRange * grid.texelsPerPixel };
	float aoCascade{ -1.0f };
	for (std::uint32_t c{}; c < grid.cascades && reach_of(c + 1, interval0) <= std::max(aoTexels, interval0); ++c)
		aoCascade = static_cast<float>(c);
	const bool hasPrevious{ previous.lightMap && settings.bounce > 0.0f };
	Uniforms base{
		.grid = { width, height, grid.origin.x, grid.origin.y },
		.probes = { width / spacing, height / spacing, spacing, grid.texelsPerPixel },
		.cascade = { 0.0f, static_cast<float>(grid.cascades), interval0, aoCascade },
		.params = { 0.0f, settings.bounce, std::clamp(settings.aoStrength, 0.0f, 1.0f), static_cast<float>(settings.debugView) },
		.ambient = { ambient, hasPrevious ? 1.0f : 0.0f },
		.view = { view.cameraPosition, std::max(view.zoom, 0.001f), static_cast<float>(std::max(lightMapDivisor, 1u)) },
		.viewport = { view.viewport, previous.view.viewport },
		.previous = { previous.view.cameraPosition, std::max(previous.view.zoom, 0.001f), settings.bilinearFix ? 1.0f : 0.0f },
	};

	// Slots: 0 scene and resolve, 1 seed and distance, then one per jump, then one per cascade.
	std::array<std::uint8_t, s_slot_stride * s_slots> slots{};
	std::uint32_t used{};
	const auto slot{ [&](const Uniforms& uniforms)
	{
		const auto offset{ used * s_slot_stride };
		std::memcpy(slots.data() + offset, &uniforms, sizeof(uniforms));
		++used;
		return offset;
	} };
	const auto sceneOffset{ slot(base) };
	std::vector<std::uint32_t> jumps{};
	for (auto step{ std::bit_ceil(std::max(grid.width, grid.height)) / 2 }; step >= 1; step /= 2)
		jumps.push_back(step);
	// One more single step pass (JFA+1) mends most of the flood's errors.
	jumps.push_back(1);
	std::vector<std::uint32_t> jumpOffsets{};
	for (const auto step : jumps)
	{
		auto uniforms{ base };
		uniforms.params.x = static_cast<float>(step);
		jumpOffsets.push_back(slot(uniforms));
	}
	std::vector<std::uint32_t> cascadeOffsets(grid.cascades);
	for (std::uint32_t c{}; c < grid.cascades; ++c)
	{
		auto uniforms{ base };
		uniforms.cascade.x = static_cast<float>(c);
		cascadeOffsets[c] = slot(uniforms);
	}
	queue.WriteBuffer(m_uniforms, 0, slots.data(), used * s_slot_stride);

	const auto beginPass{ [&](std::initializer_list<wgpu::TextureView> views, const char* label)
	{
		std::array<wgpu::RenderPassColorAttachment, 2> attachments{};
		std::uint32_t count{};
		for (const auto& target : views)
		{
			attachments[count].view = target;
			attachments[count].depthSlice = wgpu::kDepthSliceUndefined;
			attachments[count].loadOp = wgpu::LoadOp::Clear;
			attachments[count].storeOp = wgpu::StoreOp::Store;
			attachments[count].clearValue = { 0.0, 0.0, 0.0, 0.0 };
			++count;
		}
		wgpu::RenderPassDescriptor desc{};
		desc.label = label;
		desc.colorAttachmentCount = count;
		desc.colorAttachments = attachments.data();
		return encoder.BeginRenderPass(&desc);
	} };
	const auto fullscreen{ [&](const wgpu::RenderPipeline& pipeline, const wgpu::TextureView& target,
		const wgpu::BindGroup& group, std::uint32_t offset, const char* label)
	{
		auto pass{ beginPass({ target }, label) };
		pass.SetPipeline(pipeline);
		pass.SetBindGroup(0, group, 1, &offset);
		pass.Draw(3);
		pass.End();
	} };

	{
		ZoneScopedN("RadianceCascades::scene");
		auto pass{ beginPass({ m_scene.view }, "Radiance scene") };
		if (!circles.empty())
		{
			const auto group{ bind(device, {}, {}, {}, {}, {}, m_circles) };
			pass.SetPipeline(m_circlePipeline);
			pass.SetBindGroup(0, group, 1, &sceneOffset);
			pass.Draw(6, static_cast<std::uint32_t>(circles.size()));
		}
		if (occluders)
			occluders(pass, grid.view());
		if (!lights.empty())
		{
			const auto group{ bind(device, {}, {}, {}, {}, {}, m_lights) };
			pass.SetPipeline(m_emitterPipeline);
			pass.SetBindGroup(0, group, 1, &sceneOffset);
			pass.Draw(6, static_cast<std::uint32_t>(lights.size()));
		}
		pass.End();
	}

	{
		ZoneScopedN("RadianceCascades::distance");
		fullscreen(m_seedPipeline, m_jump[0].view, bind(device, m_scene.view, {}, {}, {}, {}, {}), sceneOffset, "Radiance seed");
		const std::array jumpGroups{ bind(device, {}, {}, m_jump[0].view, {}, {}, {}),
			bind(device, {}, {}, m_jump[1].view, {}, {}, {}) };
		std::size_t current{};
		for (std::size_t index{}; index < jumps.size(); ++index)
		{
			fullscreen(m_jumpPipeline, m_jump[1 - current].view, jumpGroups[current], jumpOffsets[index], "Radiance jump");
			current = 1 - current;
		}
		fullscreen(m_distancePipeline, m_distance.view, bind(device, m_scene.view, {}, m_jump[current].view, {}, {}, {}),
			sceneOffset, "Radiance distance");
	}

	{
		ZoneScopedN("RadianceCascades::cascades");
		const auto previousLight{ hasPrevious ? previous.lightMap : wgpu::TextureView{} };
		// Writing cascade c into m_cascade[c % 2] reads cascade c + 1 from the other one.
		const std::array groups{
			bind(device, {}, m_scene.view, m_distance.view, m_cascade[1].view, previousLight, m_lights),
			bind(device, {}, m_scene.view, m_distance.view, m_cascade[0].view, previousLight, m_lights) };
		for (auto c{ static_cast<int>(grid.cascades) - 1 }; c >= 0; --c)
			fullscreen(m_cascadePipeline, m_cascade[c % 2].view, groups[c % 2], cascadeOffsets[static_cast<std::size_t>(c)],
				"Radiance cascade");
	}

	m_resolveGroup = bind(device, {}, m_scene.view, m_distance.view, m_cascade[0].view, {}, {});
	m_resolveOffset = sceneOffset;
	m_ready = true;
	m_stats = { grid.width, grid.height, grid.cascades, static_cast<std::uint32_t>(lights.size()),
		static_cast<std::uint32_t>(circles.size()) };
}

void ax::RadianceCascades::draw_resolve(wgpu::RenderPassEncoder& pass) const
{
	if (!m_ready)
		return;
	pass.SetPipeline(m_resolvePipeline);
	pass.SetBindGroup(0, m_resolveGroup, 1, &m_resolveOffset);
	pass.Draw(3);
}
