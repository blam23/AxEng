#include "axeng/core/lighting/light_renderer.h"

#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>

namespace
{
	// #embed paths are resolved relative to this file. The data is not null terminated.
	constexpr unsigned char s_lighting_shader[] = {
#embed "lighting.wgsl"
	};

	constexpr auto s_light_map_format{ ax::light_map_format };
	// Frames over which the traced light fades from the old grid density to the new (about a fifth of a second).
	constexpr std::uint32_t s_density_fade_frames{ 12 };
	// Each instance is an octagon drawn as six triangles (see corner() in lighting.wgsl).
	constexpr std::uint32_t s_instance_vertices{ 18 };

	struct Uniforms
	{
		glm::vec4 viewport{};
		glm::vec4 camera{};
		glm::vec4 ambient{};
		glm::vec4 noiseTransform{};
		glm::vec4 noiseShape{};
		glm::vec4 tonemap{}; // curve, exposure, white, saturation
		glm::vec4 tonemapExtra{}; // gamma, preserve hue (0 or 1)
	};
	static_assert(sizeof(Uniforms) == 112);

	float clamped_tonemap_value(float value, float fallback, float lo, float hi) noexcept
	{
		return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
	}

	// Tonemap with every value made safe for the shader.
	ax::Tonemap sanitised(const ax::Tonemap& t) noexcept
	{
		ax::Tonemap s{ t };
		if (static_cast<std::uint32_t>(s.curve) >= static_cast<std::uint32_t>(ax::Tonemap::Curve::count))
			s.curve = ax::Tonemap::Curve::clamp;
		s.exposure = clamped_tonemap_value(t.exposure, 1.0f, 0.0f, 64.0f);
		s.white = clamped_tonemap_value(t.white, 1.0f, 0.01f, 64.0f);
		s.saturation = clamped_tonemap_value(t.saturation, 1.0f, 0.0f, 4.0f);
		s.gamma = clamped_tonemap_value(t.gamma, 1.0f, 0.1f, 8.0f);
		return s;
	}

	float aces_curve(float x) noexcept
	{
		return std::clamp(x * (2.51f * x + 0.03f) / (x * (2.43f * x + 0.59f) + 0.14f), 0.0f, 1.0f);
	}

	float hable_curve(float x) noexcept
	{
		constexpr float a{ 0.15f }, b{ 0.5f }, c{ 0.1f }, d{ 0.2f }, e{ 0.02f }, f{ 0.3f };
		return (x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f) - e / f;
	}

	// One channel curved into 0..1 (keep in step with curve() in lighting.wgsl).
	float curve_channel(const ax::Tonemap& t, float x, float maxExposure) noexcept
	{
		x = std::max(x, 0.0f);
		float c{};
		switch (t.curve)
		{
		case ax::Tonemap::Curve::reinhard: c = x * (1.0f + x / (t.white * t.white)) / (1.0f + x); break;
		case ax::Tonemap::Curve::aces: c = aces_curve(x) / std::max(aces_curve(t.white), 0.0001f); break;
		case ax::Tonemap::Curve::filmic: c = hable_curve(x) / std::max(hable_curve(t.white), 0.0001f); break;
		default: c = maxExposure > 0.0f ? x / maxExposure : 0.0f; break;
		}
		return std::clamp(c, 0.0f, 1.0f);
	}

	wgpu::BlendState additive_blend()
	{
		wgpu::BlendState blend{};
		blend.color = { wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::One };
		blend.alpha = { wgpu::BlendOperation::Add, wgpu::BlendFactor::Zero, wgpu::BlendFactor::One };
		return blend;
	}

	// Replaces the light map with the occluder's output.
	wgpu::BlendState replace_blend()
	{
		wgpu::BlendState blend{};
		blend.color = { wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::Zero };
		blend.alpha = { wgpu::BlendOperation::Add, wgpu::BlendFactor::Zero, wgpu::BlendFactor::One };
		return blend;
	}

	wgpu::BlendState noise_shadow_blend()
	{
		wgpu::BlendState blend{};
		blend.color = { wgpu::BlendOperation::Add, wgpu::BlendFactor::Zero, wgpu::BlendFactor::Src };
		blend.alpha = { wgpu::BlendOperation::Add, wgpu::BlendFactor::Zero, wgpu::BlendFactor::One };
		return blend;
	}
}

glm::vec3 ax::tonemap_light(const Tonemap& tonemap, glm::vec3 light, float maxExposure) noexcept
{
	const auto t{ sanitised(tonemap) };
	maxExposure = std::clamp(maxExposure, 0.0f, 2.0f);
	glm::vec3 x{ glm::max(light * t.exposure, glm::vec3{ 0.0f }) };
	if (t.saturation != 1.0f)
	{
		const float luma{ glm::dot(x, glm::vec3{ 0.2126f, 0.7152f, 0.0722f }) };
		x = glm::max(glm::vec3{ luma } + (x - glm::vec3{ luma }) * t.saturation, glm::vec3{ 0.0f });
	}
	glm::vec3 c{};
	if (t.preserveHue)
	{
		const float peak{ std::max({ x.r, x.g, x.b }) };
		if (peak > 0.0f) c = x * (curve_channel(t, peak, maxExposure) / peak);
	}
	else
		c = { curve_channel(t, x.r, maxExposure), curve_channel(t, x.g, maxExposure), curve_channel(t, x.b, maxExposure) };
	if (t.gamma != 1.0f) c = glm::pow(c, glm::vec3{ t.gamma });
	return c * maxExposure;
}

bool ax::lighting_active(const LightingSettings& settings, std::size_t lights, std::size_t glows, bool hasShadows) noexcept
{
	if (!settings.enabled)
		return false;
	const bool fullAmbient{ settings.ambient.r >= 1.0f && settings.ambient.g >= 1.0f && settings.ambient.b >= 1.0f };
	// A tonemap that changes full light changes even an unlit frame.
	const auto mapped{ tonemap_light(settings.tonemap, settings.ambient, settings.maxExposure) };
	const bool tonemapShows{ glm::any(glm::greaterThan(glm::abs(mapped - glm::vec3{ 1.0f }), glm::vec3{ 0.001f })) };
	// Lights only show below the exposure limit, so a fully lit scene with no glows is left untouched (unless only the
	// light is wanted). A curve that brings full ambient below 1 leaves room for them.
	return settings.lightOnly || !fullAmbient || tonemapShows || (settings.maxExposure > 1.0f && lights > 0) || hasShadows
		|| glows > 0;
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
	m_occluders = {};

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

	std::array<wgpu::BindGroupLayoutEntry, 4> applyEntries{};
	applyEntries[0] = instanceEntries[0];
	applyEntries[1].binding = 1;
	applyEntries[1].visibility = wgpu::ShaderStage::Fragment;
	applyEntries[1].texture.sampleType = wgpu::TextureSampleType::Float;
	applyEntries[1].texture.viewDimension = wgpu::TextureViewDimension::e2D;
	applyEntries[2].binding = 2;
	applyEntries[2].visibility = wgpu::ShaderStage::Fragment;
	applyEntries[2].sampler.type = wgpu::SamplerBindingType::Filtering;
	applyEntries[3] = applyEntries[1];
	applyEntries[3].binding = 3;
	wgpu::BindGroupLayoutDescriptor applyLayoutDesc{};
	applyLayoutDesc.entryCount = applyEntries.size();
	applyLayoutDesc.entries = applyEntries.data();
	m_applyLayout = device.CreateBindGroupLayout(&applyLayoutDesc);

	// Lights with marched shadows read the radiance cascades' distance field and scene (group 1).
	std::array<wgpu::BindGroupLayoutEntry, 3> marchEntries{};
	marchEntries[0].binding = 0;
	marchEntries[0].visibility = wgpu::ShaderStage::Fragment;
	marchEntries[0].buffer.type = wgpu::BufferBindingType::Uniform;
	marchEntries[0].buffer.minBindingSize = sizeof(glm::vec4) * 2;
	marchEntries[1].binding = 1;
	marchEntries[1].visibility = wgpu::ShaderStage::Fragment;
	marchEntries[1].texture.sampleType = wgpu::TextureSampleType::Uint;
	marchEntries[1].texture.viewDimension = wgpu::TextureViewDimension::e2D;
	marchEntries[2].binding = 2;
	marchEntries[2].visibility = wgpu::ShaderStage::Fragment;
	marchEntries[2].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
	marchEntries[2].texture.viewDimension = wgpu::TextureViewDimension::e2D;
	wgpu::BindGroupLayoutDescriptor marchLayoutDesc{};
	marchLayoutDesc.entryCount = marchEntries.size();
	marchLayoutDesc.entries = marchEntries.data();
	m_marchLayout = device.CreateBindGroupLayout(&marchLayoutDesc);

	const auto pipeline{ [&](const wgpu::BindGroupLayout& groupLayout, const char* vertex, const char* fragment,
		wgpu::TextureFormat format, std::uint32_t samples, const wgpu::BlendState& blend, bool marched = false)
	{
		const std::array layouts{ groupLayout, m_marchLayout };
		wgpu::PipelineLayoutDescriptor layoutDesc{};
		layoutDesc.bindGroupLayoutCount = marched ? 2 : 1;
		layoutDesc.bindGroupLayouts = layouts.data();
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
	m_lightPipeline = pipeline(m_instanceLayout, "vs_light", "fs_light", s_light_map_format, 1, additive, true);
	m_shadowPipeline = pipeline(m_instanceLayout, "vs_shadow", "fs_shadow", s_light_map_format, 1, noise_shadow_blend());
	m_occluderPipeline = pipeline(m_instanceLayout, "vs_occluder", "fs_occluder", s_light_map_format, 1, replace_blend());
	m_glowPipeline = pipeline(m_instanceLayout, "vs_glow", "fs_glow", targetFormat, sampleCount, additive);
	m_applyPipeline = pipeline(m_applyLayout, "vs_apply", "fs_apply", targetFormat, sampleCount, multiply);
	m_brightApplyPipeline = pipeline(m_applyLayout, "vs_apply", "fs_apply_bright", targetFormat, sampleCount, doubled);
	// Replaces the target's colour with the light, keeping its alpha (LightingSettings::lightOnly).
	wgpu::BlendState lightOnly{};
	lightOnly.color = { wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::Zero };
	lightOnly.alpha = { wgpu::BlendOperation::Add, wgpu::BlendFactor::Zero, wgpu::BlendFactor::One };
	m_lightOnlyPipeline = pipeline(m_applyLayout, "vs_apply", "fs_apply", targetFormat, sampleCount, lightOnly);

	wgpu::BufferDescriptor uniformDesc{};
	uniformDesc.size = sizeof(Uniforms);
	uniformDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform;
	uniformDesc.label = "Lighting uniforms";
	m_uniforms = gpu::create_buffer(device, uniformDesc, "Lighting");
	uniformDesc.size = sizeof(glm::vec4) * 2;
	uniformDesc.label = "Lighting march uniforms";
	m_marchUniforms = gpu::create_buffer(device, uniformDesc, "Lighting");
	// Its placeholder textures stand in for the distance field when the cascades do not run.
	m_cascades[0].init(device, s_light_map_format);
	m_cascades[1].init(device, s_light_map_format);
	m_timer.init(device);
	m_cascades[0].set_timer(&m_timer);
	m_cascades[1].set_timer(&m_timer);

	wgpu::SamplerDescriptor samplerDesc{};
	samplerDesc.addressModeU = wgpu::AddressMode::ClampToEdge;
	samplerDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
	samplerDesc.magFilter = wgpu::FilterMode::Linear;
	samplerDesc.minFilter = wgpu::FilterMode::Linear;
	m_sampler = device.CreateSampler(&samplerDesc);
	{
		// One white texel: no ambient occlusion.
		wgpu::TextureDescriptor whiteDesc{};
		whiteDesc.label = "Ambient occlusion factor (none)";
		whiteDesc.size = { 1, 1 };
		whiteDesc.format = wgpu::TextureFormat::RGBA16Float;
		whiteDesc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
		m_whiteFactor = gpu::create_texture(device, whiteDesc, "Lighting");
		constexpr std::array<std::uint16_t, 4> one{ 0x3C00, 0x3C00, 0x3C00, 0x3C00 };
		wgpu::TexelCopyTextureInfo destination{};
		destination.texture = m_whiteFactor;
		wgpu::TexelCopyBufferLayout layout{};
		layout.bytesPerRow = sizeof(one);
		layout.rowsPerImage = 1;
		const wgpu::Extent3D size{ 1, 1, 1 };
		device.GetQueue().WriteTexture(&destination, one.data(), sizeof(one), &layout, &size);
		m_whiteFactorView = m_whiteFactor.CreateView();
	}

	if (!m_lightPipeline || !m_shadowPipeline || !m_occluderPipeline || !m_glowPipeline || !m_applyPipeline || !m_brightApplyPipeline
		|| !m_lightOnlyPipeline)
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

	make_apply_bind_group(device, m_whiteFactorView);
	return true;
}

void ax::LightRenderer::make_apply_bind_group(const wgpu::Device& device, const wgpu::TextureView& factor)
{
	std::array<wgpu::BindGroupEntry, 4> entries{};
	entries[0].binding = 0;
	entries[0].buffer = m_uniforms;
	entries[0].size = sizeof(Uniforms);
	entries[1].binding = 1;
	entries[1].textureView = m_lightMap->view();
	entries[2].binding = 2;
	entries[2].sampler = m_sampler;
	entries[3].binding = 3;
	entries[3].textureView = factor;
	wgpu::BindGroupDescriptor groupDesc{};
	groupDesc.layout = m_applyLayout;
	groupDesc.entryCount = entries.size();
	groupDesc.entries = entries.data();
	m_applyBindGroup = device.CreateBindGroup(&groupDesc);
	m_applyFactorView = factor;
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
	instances.buffer = gpu::create_buffer(device, desc, "Lighting");
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
	const std::function<void(wgpu::RenderPassEncoder&, std::uint32_t)>& extraLights, const NoiseShadow2D* noiseShadow,
	std::span<const LightOccluder2D> occluders, std::span<const RadianceCircle> radianceCircles,
	const RadianceCascades::OccluderDraw& radianceOccluders, std::uint64_t radianceOccluderSignature,
	const AmbientOcclusion2D* ambientOcclusion, const AmbientOcclusion::CasterDraw& aoCasters)
{
	ZoneScopedN("LightRenderer::encode");
	m_stats = {};
	if (!m_lightPipeline || width == 0 || height == 0)
		return;
	m_timer.begin_frame();
	const auto divisor{ std::clamp<std::uint32_t>(settings.resolutionDivisor, 1, 4) };
	const auto mapWidth{ std::max<std::uint32_t>(1, (width + divisor - 1) / divisor) };
	const auto mapHeight{ std::max<std::uint32_t>(1, (height + divisor - 1) / divisor) };
	const bool freshLightMap{ !m_lightMap || m_lightMap->width() != mapWidth || m_lightMap->height() != mapHeight };
	if (!ensure_light_map(device, mapWidth, mapHeight))
		return;

	// Radiance cascades: lights that may cast shadows, before the first occluder (so deck layering still works), are
	// traced and resolved into the light map instead of drawn.
	const auto& radiance{ settings.radiance };
	bool traced{}, radianceRan{};
	if (radiance.enabled && active_cascades().init(device, light_map_format))
	{
		const auto firstOccluded{ occluders.empty() ? lights.size()
			: std::min<std::size_t>(occluders.front().firstLight, lights.size()) };
		float reach{ radiance.aoStrength > 0.0f ? radiance.aoRange : 0.0f };
		const auto anyGrid{ radiance_grid(view, radiance, 1.0f) };
		// Marched lights (and shadowed ones that end up marched, outside the grid) need the distance field but not the
		// cascades' reach.
		bool marched{};
		for (std::size_t index{}; index < firstOccluded; ++index)
		{
			if (radiance_traces(lights[index], anyGrid, radiance))
				reach = std::max(reach, lights[index].radius);
			marched = marched || (lights[index].shadows > 0.0f && lights[index].length <= 0.0f);
		}
		const bool wanted{ reach > 0.0f || marched || radiance.debugView != 0 };
		const auto grid{ wanted ? radiance_grid(view, radiance, reach) : RadianceGrid{} };
		// A change of density: the cascades last used fade out over the next few frames, as they last were.
		if (grid.width > 0)
		{
			if (m_lastTexelsPerPixel > 0.0f && grid.texelsPerPixel != m_lastTexelsPerPixel)
			{
				m_active = 1 - m_active;
				m_fadeLeft = s_density_fade_frames;
				active_cascades().init(device, light_map_format);
			}
			m_lastTexelsPerPixel = grid.texelsPerPixel;
		}
		else
			m_fadeLeft = 0;
		m_tracedLights.clear();
		m_analyticLights.clear();
		for (std::size_t index{}; index < lights.size(); ++index)
		{
			const auto& light{ lights[index] };
			if (index < firstOccluded && m_tracedLights.size() < radiance_max_lights && radiance_traces(light, grid, radiance))
			{
				m_tracedLights.push_back(light);
				continue;
			}
			m_analyticLights.push_back(light);
			// A light that would be traced but cannot be (outside the grid, too small, or one too many) marches its
			// shadows instead: otherwise a lamp crossing the grid's edge as the view moves or zooms would lose its shadows
			// at once, lighting what they covered.
			if (index < firstOccluded && grid.width > 0 && light.shadows > 0.0f && light.shadows < 1.5f
				&& light.length <= 0.0f && light.intensity > 0.0f)
				m_analyticLights.back().shadows = Light2D::marched_shadows;
		}
		if (grid.width > 0)
		{
			const RadianceCascades::Previous previous{
				.lightMap = (m_lightMapDrawn && !freshLightMap) ? m_lightMap->view() : wgpu::TextureView{},
				.view = m_previousView, .divisor = m_previousDivisor };
			active_cascades().encode(device, queue, encoder, grid, radiance, view, divisor, settings.ambient, m_tracedLights,
				radianceCircles, radianceOccluders, previous, radianceOccluderSignature);
			traced = active_cascades().ready();
			radianceRan = traced;
			// The old grid's last result fades out, pointed at this view but not traced again.
			if (m_fadeLeft > 0)
			{
				auto& fading{ m_cascades[1 - m_active] };
				if (fading.ready()) fading.refresh_resolve(queue, view, divisor, settings.ambient);
				else m_fadeLeft = 0;
			}
		}
		if (traced)
		{
			// Each traced light's near part is drawn plainly, in its place before any occluder.
			m_nearLights.clear();
			for (const auto& light : m_tracedLights) m_nearLights.push_back(radiance_near_part(light, grid));
			m_analyticLights.insert(m_analyticLights.begin(), m_nearLights.begin(), m_nearLights.end());
			m_shiftedOccluders.assign(occluders.begin(), occluders.end());
			lights = m_analyticLights;
			occluders = m_shiftedOccluders;
		}
	}

	const Uniforms uniforms{
		.viewport = { view.viewport.x, view.viewport.y, 0.0f, 0.0f },
		.camera = { view.cameraPosition.x, view.cameraPosition.y, std::max(view.zoom, 0.001f), 0.0f },
		.ambient = { settings.ambient, std::clamp(settings.maxExposure, 0.0f, 2.0f) },
		.noiseTransform = noiseShadow ? glm::vec4{ noiseShadow->scale, noiseShadow->offset } : glm::vec4{},
		.noiseShape = noiseShadow ? glm::vec4{ noiseShadow->coverage, noiseShadow->softness,
			noiseShadow->darkness, noiseShadow->seed } : glm::vec4{},
		.tonemap = [&]
		{
			const auto t{ sanitised(settings.tonemap) };
			return glm::vec4{ static_cast<float>(t.curve), t.exposure, t.white, t.saturation };
		}(),
		.tonemapExtra = { sanitised(settings.tonemap).gamma, settings.tonemap.preserveHue ? 1.0f : 0.0f, 0.0f, 0.0f },
	};
	// The light map is lower resolution than the scene; retain the scale for converting its fragment coordinates
	// back to the full-resolution screen before transforming them into world space.
	const auto mappedUniforms{ [&]
	{
		auto mapped{ uniforms };
		mapped.camera.w = static_cast<float>(divisor);
		return mapped;
	}() };
	queue.WriteBuffer(m_uniforms, 0, &mappedUniforms, sizeof(mappedUniforms));
	{
		const auto march{ active_cascades().march_inputs(radianceRan) };
		const std::array marchUniforms{ march.grid, march.size };
		queue.WriteBuffer(m_marchUniforms, 0, marchUniforms.data(), sizeof(marchUniforms));
		std::array<wgpu::BindGroupEntry, 3> entries{};
		entries[0].binding = 0;
		entries[0].buffer = m_marchUniforms;
		entries[0].size = sizeof(marchUniforms);
		entries[1].binding = 1;
		entries[1].textureView = march.distance;
		entries[2].binding = 2;
		entries[2].textureView = march.scene;
		wgpu::BindGroupDescriptor groupDesc{};
		groupDesc.layout = m_marchLayout;
		groupDesc.entryCount = entries.size();
		groupDesc.entries = entries.data();
		m_marchGroup = device.CreateBindGroup(&groupDesc);
	}

	// Soft shadows round things that stand up: a factor the light is multiplied by once it is clamped.
	const bool occluded{ ambientOcclusion && ambientOcclusion->darkness > 0.0f && m_ambientOcclusion.init(device)
		&& m_ambientOcclusion.encode(device, queue, encoder, view, divisor, mapWidth, mapHeight, *ambientOcclusion,
			aoCasters, &m_timer) };
	if (const auto& factor{ occluded ? m_ambientOcclusion.factor_view() : m_whiteFactorView };
		factor.Get() != m_applyFactorView.Get())
		make_apply_bind_group(device, factor);

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
		// The resolve, the drawn lights (marched ones and traced lights' near parts among them) and occluders.
		passDesc.timestampWrites = m_timer.pass("GPU ms: light map");
		auto pass{ encoder.BeginRenderPass(&passDesc) };
		if (traced)
		{
			active_cascades().draw_resolve(pass);
			// Fading out from the old density.
			if (m_fadeLeft > 0)
			{
				m_cascades[1 - m_active].draw_resolve(pass,
					static_cast<float>(m_fadeLeft) / static_cast<float>(s_density_fade_frames + 1));
				--m_fadeLeft;
			}
		}
		if (!lights.empty())
		{
			const auto bytes{ lights.size_bytes() };
			ensure_buffer(device, m_lights, bytes, "Lights");
			queue.WriteBuffer(m_lights.buffer, 0, lights.data(), bytes);
		}
		if (!occluders.empty())
		{
			// Two vec4s of corners each.
			m_occluderScratch.clear();
			for (const auto& occluder : occluders)
				m_occluderScratch.insert(m_occluderScratch.end(), occluder.corners.begin(), occluder.corners.end());
			const auto bytes{ m_occluderScratch.size() * sizeof(glm::vec2) };
			ensure_buffer(device, m_occluders, bytes, "Light occluders");
			queue.WriteBuffer(m_occluders.buffer, 0, m_occluderScratch.data(), bytes);
		}
		// The lights between occluders, in order, each occluder hiding all drawn before it.
		const auto count{ static_cast<std::uint32_t>(lights.size()) };
		std::uint32_t drawn{};
		const auto drawLights{ [&](std::uint32_t end)
		{
			end = std::min(end, count);
			if (end <= drawn) return;
			pass.SetPipeline(m_lightPipeline);
			pass.SetBindGroup(0, m_lights.bindGroup);
			pass.SetBindGroup(1, m_marchGroup);
			pass.Draw(s_instance_vertices, end - drawn, 0, drawn);
			drawn = end;
		} };
		for (std::size_t index{}; index < occluders.size(); ++index)
		{
			drawLights(occluders[index].firstLight);
			if (index == 0 && extraLights)
				extraLights(pass, 0);
			pass.SetPipeline(m_occluderPipeline);
			pass.SetBindGroup(0, m_occluders.bindGroup);
			pass.Draw(6, 1, 0, static_cast<std::uint32_t>(index));
			if (extraLights && occluders[index].layer != 0)
				extraLights(pass, occluders[index].layer);
		}
		drawLights(count);
		if (occluders.empty() && extraLights)
			extraLights(pass, 0);
		if (noiseShadow && noiseShadow->darkness > 0.0f)
		{
			if (m_lights.buffer == nullptr) ensure_buffer(device, m_lights, sizeof(Light2D), "Noise shadow binding");
			pass.SetPipeline(m_shadowPipeline);
			pass.SetBindGroup(0, m_lights.bindGroup);
			pass.Draw(3);
		}
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
		passDesc.timestampWrites = m_timer.pass("GPU ms: light apply and glows");
		auto pass{ encoder.BeginRenderPass(&passDesc) };
		pass.SetPipeline(settings.lightOnly ? m_lightOnlyPipeline
			: settings.maxExposure > 1.0f ? m_brightApplyPipeline : m_applyPipeline);
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

	m_stats = { static_cast<std::uint32_t>(lights.size()), static_cast<std::uint32_t>(glows.size()), mapWidth, mapHeight,
		static_cast<std::uint32_t>(occluders.size()) };
	if (traced)
	{
		const auto radianceStats{ active_cascades().last_stats() };
		m_stats.radianceWidth = radianceStats.width;
		m_stats.radianceHeight = radianceStats.height;
		m_stats.radianceCascades = radianceStats.cascades;
		m_stats.radianceLights = radianceStats.lights;
		// Every light lit the frame, traced or drawn (a traced light's near part is not another light).
		m_stats.lights += radianceStats.lights;
		m_stats.lights -= std::min(m_stats.lights, static_cast<std::uint32_t>(m_nearLights.size()));
		m_stats.radianceCircles = radianceStats.circles;
		m_stats.radianceReused = active_cascades().reused();
	}
	m_stats.ambientOcclusion = occluded;
	m_previousView = view;
	m_previousDivisor = divisor;
	m_lightMapDrawn = true;
	m_timer.end_frame(encoder);
}
