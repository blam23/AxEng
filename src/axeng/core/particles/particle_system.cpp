#include "axeng/core/particles/particle_system.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <stdexcept>

namespace
{
	constexpr std::uint32_t s_max_capacity{ 65536 };
	constexpr std::uint32_t s_workgroup_size{ 64 };
	constexpr double s_max_frame_delta{ 10.0 };
	constexpr double s_particle_clock_period{ 4096.0 };
	constexpr float s_min_emission_duration{ 0.01f };
	constexpr float s_tau{ 6.28318530718f };

	struct ParticleGpu
	{
		glm::vec4 originVelocity{};
		glm::vec4 ageLifeRotationAngular{};
		glm::uvec4 meta{};
	};

	struct alignas(16) ParticleParametersGpu
	{
		std::array<float, 4> cameraBounds{};
		std::array<float, 4> transform{};
		std::array<float, 4> time{};
		std::array<float, 4> lifeSpeed{};
		std::array<float, 4> directionSpreadRadiusSize{};
		std::array<float, 4> spawnArea{};
		std::array<float, 4> appearance{};
		std::array<float, 4> rotationRange{};
		std::array<float, 4> colorStart{};
		std::array<float, 4> colorEnd{};
		std::array<float, 4> gravity{};
		std::array<std::uint32_t, 4> misc{};
	};

	struct DrawArguments
	{
		std::uint32_t vertexCount{ 6 };
		std::uint32_t instanceCount{};
		std::uint32_t firstVertex{};
		std::uint32_t firstInstance{};
	};

	static_assert(sizeof(ParticleGpu) == 48);
	static_assert(sizeof(ParticleParametersGpu) == 192);
	static_assert(sizeof(DrawArguments) == 16);

	wgpu::Buffer create_buffer(const wgpu::Device& device, std::uint64_t size, wgpu::BufferUsage usage, const char* label)
	{
		wgpu::BufferDescriptor desc{};
		desc.size = size;
		desc.usage = usage;
		desc.label = label;
		return device.CreateBuffer(&desc);
	}

	wgpu::BindGroupLayoutEntry storage_entry(std::uint32_t binding, wgpu::ShaderStage visibility,
		wgpu::BufferBindingType type)
	{
		wgpu::BindGroupLayoutEntry entry{};
		entry.binding = binding;
		entry.visibility = visibility;
		entry.buffer.type = type;
		return entry;
	}

	bool intersects(const glm::vec4& a, const glm::vec4& b)
	{
		return a.x <= b.z && a.z >= b.x && a.y <= b.w && a.w >= b.y;
	}

}

std::optional<std::string> ax::validate_particle_config(const ParticleConfig& config)
{
	const auto finite = [](float value) { return std::isfinite(value); };
	if (config.capacity == 0 || config.capacity > s_max_capacity)
		return std::format("capacity must be between 1 and {}", s_max_capacity);
	if (!finite(config.emissionRate) || config.emissionRate < 0.0f || config.emissionRate > 100000.0f)
		return "emission_rate must be finite and between 0 and 100000";
	if (!finite(config.duration) || config.duration < s_min_emission_duration || config.duration > 3600.0f)
		return "duration must be finite and between 0.01 and 3600";
	if (!finite(config.explosiveness) || config.explosiveness < 0.0f || config.explosiveness > 1.0f)
		return "explosiveness must be between 0 and 1";
	if (!finite(config.position.x) || !finite(config.position.y) || !finite(config.rotation)
		|| !finite(config.direction) || !finite(config.spread) || !finite(config.spawnRadius)
		|| !finite(config.spawnSize.x) || !finite(config.spawnSize.y)
		|| !finite(config.particleRotationMin) || !finite(config.particleRotationMax)
		|| !finite(config.angularVelocityMin) || !finite(config.angularVelocityMax)
		|| !finite(config.lifetimeMin) || !finite(config.lifetimeMax)
		|| !finite(config.speedMin) || !finite(config.speedMax)
		|| !finite(config.sizeStart) || !finite(config.sizeEnd)
		|| !finite(config.gravity.x) || !finite(config.gravity.y) || !finite(config.z))
		return "particle parameters must be finite numbers";
	if (config.lifetimeMin <= 0.0f || config.lifetimeMax < config.lifetimeMin)
		return "lifetime must have positive min and max >= min";
	if (config.speedMin < 0.0f || config.speedMax < config.speedMin)
		return "speed must have non-negative min and max >= min";
	if (config.spread < 0.0f || config.spread > s_tau || config.spawnRadius < 0.0f)
		return "spread must be in 0..2*pi and spawn_radius must be non-negative";
	if (config.spawnSize.x < 0.0f || config.spawnSize.y < 0.0f
		|| static_cast<std::uint32_t>(config.spawnShape) > static_cast<std::uint32_t>(ParticleSpawnShape::Rectangle))
		return "spawn_size must be non-negative and spawn_shape must be point, circle, or rectangle";
	if (config.particleRotationMax < config.particleRotationMin
		|| config.angularVelocityMax < config.angularVelocityMin)
		return "particle rotation and angular velocity ranges must have max >= min";
	if (config.sizeStart < 0.0f || config.sizeEnd < 0.0f)
		return "particle sizes must be non-negative";
	for (const auto value : { config.colorStart.r, config.colorStart.g, config.colorStart.b, config.colorStart.a,
		config.colorEnd.r, config.colorEnd.g, config.colorEnd.b, config.colorEnd.a })
		if (!finite(value) || value < 0.0f || value > 1.0f)
			return "particle color components must be between 0 and 1";
	return std::nullopt;
}

ax::ParticleEmitter::ParticleEmitter(ParticleConfig config)
	: m_config{ std::move(config) }
{
	m_clearRequested = true;
}

void ax::ParticleEmitter::check_thread() const
{
	if (m_thread != std::this_thread::get_id())
		throw std::runtime_error("Particle emitters belong to the renderer thread");
}

void ax::ParticleEmitter::set_config(const ParticleConfig& config)
{
	check_thread();
	if (const auto error{ validate_particle_config(config) })
		throw std::invalid_argument(*error);
	if (config.capacity != m_config.capacity)
		throw std::invalid_argument("capacity cannot be changed after emitter creation");
	m_config = config;
}

void ax::ParticleEmitter::set_position(glm::vec2 position)
{
	check_thread();
	m_config.position = position;
}

void ax::ParticleEmitter::set_rotation(float rotation)
{
	check_thread();
	m_config.rotation = rotation;
}

void ax::ParticleEmitter::play()
{
	check_thread();
	m_elapsed = 0.0;
	m_systemTime = 0.0;
	m_sinceLastCompute = 0.0;
	m_emissionRemainder = 0.0;
	m_config.playing = true;
	m_clearRequested = true;
}

void ax::ParticleEmitter::stop() noexcept
{
	m_config.playing = false;
}

void ax::ParticleEmitter::clear() noexcept
{
	m_clearRequested = true;
}

void ax::ParticleEmitter::set_visible(bool visible) noexcept
{
	m_config.visible = visible;
}

bool ax::ParticleEmitter::finished() const noexcept
{
	return !m_config.loop && !m_config.playing && m_elapsed >= m_config.duration;
}

void ax::ParticleEmitter::update_world_bounds()
{
	const auto& config{ m_config };
	const auto lifetime{ config.lifetimeMax };
	const auto spawnExtent{ config.spawnShape == ParticleSpawnShape::Rectangle
		? std::max(config.spawnSize.x, config.spawnSize.y) * 0.5f : config.spawnRadius };
	const auto extentX{ spawnExtent + config.speedMax * lifetime
		+ 0.5f * std::abs(config.gravity.x) * lifetime * lifetime
		+ std::max(config.sizeStart, config.sizeEnd) * 0.5f };
	const auto extentY{ spawnExtent + config.speedMax * lifetime
		+ 0.5f * std::abs(config.gravity.y) * lifetime * lifetime
		+ std::max(config.sizeStart, config.sizeEnd) * 0.5f };
	const glm::vec4 reach{
		config.position.x - extentX, config.position.y - extentY,
		config.position.x + extentX, config.position.y + extentY
	};
	if (!m_hasWorldBounds)
	{
		m_worldBounds = reach;
		m_hasWorldBounds = true;
		return;
	}
	m_worldBounds = {
		std::min(m_worldBounds.x, reach.x),
		std::min(m_worldBounds.y, reach.y),
		std::max(m_worldBounds.z, reach.z),
		std::max(m_worldBounds.w, reach.w)
	};
}

std::uint32_t ax::ParticleEmitter::advance(double delta)
{
	if (!m_config.playing || delta <= 0.0)
		return 0;

	auto remaining{ delta };
	std::uint32_t spawnCount{};
	while (remaining > 0.0 && m_config.playing)
	{
		const auto duration{ static_cast<double>(m_config.duration) };
		const auto phase{ std::fmod(m_elapsed, duration) };
		const auto untilCycleEnd{ duration - phase };
		const auto step{ std::min(remaining, untilCycleEnd) };
		const auto explosiveness{ static_cast<double>(m_config.explosiveness) };
		const auto emissionWindow{ duration * (1.0 - explosiveness) };

		if (explosiveness >= 1.0 && phase < 1e-9)
		{
			m_emissionRemainder += static_cast<double>(m_config.emissionRate) * duration;
			const auto due{ static_cast<std::uint32_t>(std::floor(m_emissionRemainder)) };
			m_emissionRemainder -= due;
			spawnCount += due;
		}
		else if (phase < emissionWindow)
		{
			const auto overlap{ std::max(0.0, std::min(phase + step, emissionWindow) - phase) };
			const auto normalizedRate{ static_cast<double>(m_config.emissionRate) / (1.0 - explosiveness) };
			m_emissionRemainder += normalizedRate * overlap;
			const auto due{ static_cast<std::uint32_t>(std::floor(m_emissionRemainder)) };
			m_emissionRemainder -= due;
			spawnCount += due;
		}

		m_elapsed += step;
		remaining -= step;
		if (!m_config.loop && m_elapsed >= duration)
			m_config.playing = false;
		if (step <= 0.0)
			break;
	}
	return std::min(spawnCount, s_max_capacity);
}

std::shared_ptr<ax::ParticleEmitter> ax::ParticleSystem::create(const ParticleConfig& config)
{
	if (m_thread != std::this_thread::get_id())
		throw std::runtime_error("Particle systems belong to the renderer thread");
	if (const auto error{ validate_particle_config(config) })
		throw std::invalid_argument(*error);
	auto emitter{ std::shared_ptr<ParticleEmitter>{ new ParticleEmitter{ config } } };
	m_emitters.emplace_back(emitter);
	return emitter;
}

void ax::ParticleSystem::initialise(wgpu::Device& device, wgpu::TextureFormat colourFormat, std::uint32_t sampleCount)
{
	if (m_device.Get() == device.Get() && m_computePipeline.Get() != nullptr
		&& m_colourFormat == colourFormat && m_sampleCount == sampleCount)
		return;

	for (const auto& weakEmitter : m_emitters)
	{
		if (const auto emitter{ weakEmitter.lock() })
		{
			emitter->m_particleBuffer = nullptr;
			emitter->m_uniformBuffer = nullptr;
			emitter->m_spawnCounterBuffer = nullptr;
			emitter->m_visibleBuffer = nullptr;
			emitter->m_drawArgumentsBuffer = nullptr;
			emitter->m_computeBindGroup = nullptr;
			emitter->m_renderBindGroup = nullptr;
			emitter->m_clearRequested = true;
		}
	}
	m_frameEmitters.clear();
	m_renderPipeline = nullptr;
	m_computePipeline = nullptr;
	m_renderPipelineLayout = nullptr;
	m_computePipelineLayout = nullptr;
	m_renderLayout = nullptr;
	m_computeLayout = nullptr;
	m_device = device;
	m_colourFormat = colourFormat;
	m_sampleCount = sampleCount;

	constexpr unsigned char s_shader[] = {
#embed "particles.wgsl"
	};
	wgpu::ShaderSourceWGSL source{};
	source.code = { reinterpret_cast<const char*>(s_shader), sizeof(s_shader) };
	wgpu::ShaderModuleDescriptor shaderDesc{};
	shaderDesc.nextInChain = &source;
	shaderDesc.label = "ParticleShader";
	const auto shader{ device.CreateShaderModule(&shaderDesc) };

	std::array<wgpu::BindGroupLayoutEntry, 5> computeEntries
	{
		storage_entry(0, wgpu::ShaderStage::Compute, wgpu::BufferBindingType::Storage),
		{},
		storage_entry(2, wgpu::ShaderStage::Compute, wgpu::BufferBindingType::Storage),
		storage_entry(3, wgpu::ShaderStage::Compute, wgpu::BufferBindingType::Storage),
		storage_entry(4, wgpu::ShaderStage::Compute, wgpu::BufferBindingType::Storage),
	};
	computeEntries[1].binding = 1;
	computeEntries[1].visibility = wgpu::ShaderStage::Compute;
	computeEntries[1].buffer.type = wgpu::BufferBindingType::Uniform;
	wgpu::BindGroupLayoutDescriptor computeLayoutDesc{};
	computeLayoutDesc.entryCount = static_cast<std::uint32_t>(computeEntries.size());
	computeLayoutDesc.entries = computeEntries.data();
	m_computeLayout = device.CreateBindGroupLayout(&computeLayoutDesc);

	std::array<wgpu::BindGroupLayoutEntry, 3> renderEntries
	{
		storage_entry(0, wgpu::ShaderStage::Vertex, wgpu::BufferBindingType::ReadOnlyStorage),
		{},
		storage_entry(2, wgpu::ShaderStage::Vertex, wgpu::BufferBindingType::ReadOnlyStorage),
	};
	renderEntries[1].binding = 1;
	renderEntries[1].visibility = wgpu::ShaderStage::Vertex;
	renderEntries[1].buffer.type = wgpu::BufferBindingType::Uniform;
	wgpu::BindGroupLayoutDescriptor renderLayoutDesc{};
	renderLayoutDesc.entryCount = static_cast<std::uint32_t>(renderEntries.size());
	renderLayoutDesc.entries = renderEntries.data();
	m_renderLayout = device.CreateBindGroupLayout(&renderLayoutDesc);
	wgpu::BindGroupLayoutDescriptor emptyLayoutDesc{};
	m_emptyLayout = device.CreateBindGroupLayout(&emptyLayoutDesc);
	wgpu::BindGroupDescriptor emptyGroupDesc{};
	emptyGroupDesc.layout = m_emptyLayout;
	m_emptyBindGroup = device.CreateBindGroup(&emptyGroupDesc);

	wgpu::PipelineLayoutDescriptor computePipelineLayoutDesc{};
	computePipelineLayoutDesc.bindGroupLayoutCount = 1;
	computePipelineLayoutDesc.bindGroupLayouts = &m_computeLayout;
	m_computePipelineLayout = device.CreatePipelineLayout(&computePipelineLayoutDesc);

	wgpu::BindGroupLayout renderLayouts[2]{ m_emptyLayout, m_renderLayout };
	wgpu::PipelineLayoutDescriptor renderPipelineLayoutDesc{};
	renderPipelineLayoutDesc.bindGroupLayoutCount = 2;
	renderPipelineLayoutDesc.bindGroupLayouts = renderLayouts;
	m_renderPipelineLayout = device.CreatePipelineLayout(&renderPipelineLayoutDesc);

	wgpu::ComputePipelineDescriptor computePipelineDesc{};
	computePipelineDesc.layout = m_computePipelineLayout;
	computePipelineDesc.compute.module = shader;
	computePipelineDesc.compute.entryPoint = "update_particles";
	computePipelineDesc.label = "ParticleComputePipeline";
	m_computePipeline = device.CreateComputePipeline(&computePipelineDesc);

	wgpu::RenderPipelineDescriptor renderPipelineDesc{};
	renderPipelineDesc.layout = m_renderPipelineLayout;
	renderPipelineDesc.vertex.module = shader;
	renderPipelineDesc.vertex.entryPoint = "render_particle";
	renderPipelineDesc.vertex.bufferCount = 0;
	renderPipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
	renderPipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;
	renderPipelineDesc.primitive.cullMode = wgpu::CullMode::None;
	renderPipelineDesc.multisample.count = sampleCount;
	renderPipelineDesc.multisample.mask = ~0u;

	wgpu::DepthStencilState depthState{};
	depthState.format = wgpu::TextureFormat::Depth24Plus;
	depthState.depthWriteEnabled = wgpu::OptionalBool::False;
	depthState.depthCompare = wgpu::CompareFunction::Greater;
	renderPipelineDesc.depthStencil = &depthState;

	wgpu::BlendState accumulationBlend{};
	accumulationBlend.color.srcFactor = wgpu::BlendFactor::One;
	accumulationBlend.color.dstFactor = wgpu::BlendFactor::One;
	accumulationBlend.color.operation = wgpu::BlendOperation::Add;
	accumulationBlend.alpha.srcFactor = wgpu::BlendFactor::One;
	accumulationBlend.alpha.dstFactor = wgpu::BlendFactor::One;
	accumulationBlend.alpha.operation = wgpu::BlendOperation::Add;
	wgpu::BlendState revealageBlend{};
	revealageBlend.color.srcFactor = wgpu::BlendFactor::Zero;
	revealageBlend.color.dstFactor = wgpu::BlendFactor::OneMinusSrc;
	revealageBlend.color.operation = wgpu::BlendOperation::Add;
	wgpu::ColorTargetState targets[2]{};
	targets[0].format = wgpu::TextureFormat::RGBA16Float;
	targets[0].blend = &accumulationBlend;
	targets[0].writeMask = wgpu::ColorWriteMask::All;
	targets[1].format = wgpu::TextureFormat::R8Unorm;
	targets[1].blend = &revealageBlend;
	targets[1].writeMask = wgpu::ColorWriteMask::All;
	wgpu::FragmentState fragment{};
	fragment.module = shader;
	fragment.entryPoint = "render_particle_fragment";
	fragment.targetCount = 2;
	fragment.targets = targets;
	renderPipelineDesc.fragment = &fragment;
	renderPipelineDesc.label = "ParticleRenderPipeline";
	m_renderPipeline = device.CreateRenderPipeline(&renderPipelineDesc);
}

void ax::ParticleSystem::encode_compute(wgpu::Device& device, wgpu::Queue& queue, wgpu::CommandEncoder& encoder,
	double delta, glm::vec4 cameraBounds, glm::vec2 viewport, float zoom,
	wgpu::TextureFormat colourFormat, std::uint32_t sampleCount)
{
	if (m_thread != std::this_thread::get_id())
		throw std::runtime_error("Particle systems belong to the renderer thread");
	initialise(device, colourFormat, sampleCount);
	m_frameEmitters.clear();

	for (auto it{ m_emitters.begin() }; it != m_emitters.end();)
	{
		auto emitter{ it->lock() };
		if (!emitter)
		{
			it = m_emitters.erase(it);
			continue;
		}
		emitter->check_thread();
		++it;

		if (!emitter->m_particleBuffer.Get())
		{
			const auto capacity{ emitter->m_config.capacity };
			emitter->m_particleBuffer = create_buffer(device,
				static_cast<std::uint64_t>(capacity) * sizeof(ParticleGpu),
				wgpu::BufferUsage::Storage, "ParticleState");
			emitter->m_uniformBuffer = create_buffer(device, sizeof(ParticleParametersGpu),
				wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst, "ParticleParameters");
			emitter->m_spawnCounterBuffer = create_buffer(device, sizeof(std::uint32_t),
				wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst, "ParticleSpawnCounter");
			emitter->m_visibleBuffer = create_buffer(device,
				static_cast<std::uint64_t>(capacity) * sizeof(std::uint32_t),
				wgpu::BufferUsage::Storage, "ParticleVisibleIndices");
			emitter->m_drawArgumentsBuffer = create_buffer(device, sizeof(DrawArguments),
				wgpu::BufferUsage::Storage | wgpu::BufferUsage::Indirect | wgpu::BufferUsage::CopyDst,
				"ParticleDrawArguments");

			wgpu::BindGroupEntry computeEntries[5]{};
			computeEntries[0].binding = 0;
			computeEntries[0].buffer = emitter->m_particleBuffer;
			computeEntries[0].size = static_cast<std::uint64_t>(capacity) * sizeof(ParticleGpu);
			computeEntries[1].binding = 1;
			computeEntries[1].buffer = emitter->m_uniformBuffer;
			computeEntries[1].size = sizeof(ParticleParametersGpu);
			computeEntries[2].binding = 2;
			computeEntries[2].buffer = emitter->m_spawnCounterBuffer;
			computeEntries[2].size = sizeof(std::uint32_t);
			computeEntries[3].binding = 3;
			computeEntries[3].buffer = emitter->m_visibleBuffer;
			computeEntries[3].size = static_cast<std::uint64_t>(capacity) * sizeof(std::uint32_t);
			computeEntries[4].binding = 4;
			computeEntries[4].buffer = emitter->m_drawArgumentsBuffer;
			computeEntries[4].size = sizeof(DrawArguments);
			wgpu::BindGroupDescriptor computeGroupDesc{};
			computeGroupDesc.layout = m_computeLayout;
			computeGroupDesc.entryCount = 5;
			computeGroupDesc.entries = computeEntries;
			emitter->m_computeBindGroup = device.CreateBindGroup(&computeGroupDesc);

			wgpu::BindGroupEntry renderEntries[3]{};
			renderEntries[0].binding = 0;
			renderEntries[0].buffer = emitter->m_particleBuffer;
			renderEntries[0].size = static_cast<std::uint64_t>(capacity) * sizeof(ParticleGpu);
			renderEntries[1].binding = 1;
			renderEntries[1].buffer = emitter->m_uniformBuffer;
			renderEntries[1].size = sizeof(ParticleParametersGpu);
			renderEntries[2].binding = 2;
			renderEntries[2].buffer = emitter->m_visibleBuffer;
			renderEntries[2].size = static_cast<std::uint64_t>(capacity) * sizeof(std::uint32_t);
			wgpu::BindGroupDescriptor renderGroupDesc{};
			renderGroupDesc.layout = m_renderLayout;
			renderGroupDesc.entryCount = 3;
			renderGroupDesc.entries = renderEntries;
			emitter->m_renderBindGroup = device.CreateBindGroup(&renderGroupDesc);
		}

		const auto frameDelta{ std::clamp(delta, 0.0, s_max_frame_delta) };
		emitter->m_systemTime = std::fmod(emitter->m_systemTime + frameDelta, s_particle_clock_period);
		emitter->m_sinceLastCompute += frameDelta;
		emitter->m_spawnCount = emitter->advance(frameDelta);
		const auto& config{ emitter->m_config };
		emitter->update_world_bounds();
		if (!config.visible || !intersects(emitter->m_worldBounds, cameraBounds))
			continue;
		if (emitter->m_sinceLastCompute >= s_particle_clock_period)
			emitter->m_clearRequested = true;

		ParticleParametersGpu params{};
		params.cameraBounds = { cameraBounds.x, cameraBounds.y, cameraBounds.z, cameraBounds.w };
		params.transform = { config.position.x, config.position.y, config.rotation, config.z };
		params.time = {
			static_cast<float>(emitter->m_systemTime),
			static_cast<float>(emitter->m_spawnCount),
			config.duration,
			config.explosiveness
		};
		params.lifeSpeed = { config.lifetimeMin, config.lifetimeMax, config.speedMin, config.speedMax };
		params.directionSpreadRadiusSize = { config.direction, config.spread, config.spawnRadius, config.sizeStart };
		params.spawnArea = { config.spawnSize.x, config.spawnSize.y, 0.0f, 0.0f };
		params.appearance = { config.sizeEnd, viewport.x, viewport.y, zoom };
		params.rotationRange = { config.particleRotationMin, config.particleRotationMax,
			config.angularVelocityMin, config.angularVelocityMax };
		params.colorStart = { config.colorStart.r, config.colorStart.g, config.colorStart.b, config.colorStart.a };
		params.colorEnd = { config.colorEnd.r, config.colorEnd.g, config.colorEnd.b, config.colorEnd.a };
		params.gravity = { config.gravity.x, config.gravity.y, 0.0f, 0.0f };
		const auto flags{ (emitter->m_clearRequested ? 1u : 0u)
			| (config.localSpace ? 2u : 0u)
			| (static_cast<std::uint32_t>(config.spawnShape) << 2u) };
		params.misc = { config.capacity, config.seed, emitter->m_frame++, flags };

		queue.WriteBuffer(emitter->m_uniformBuffer, 0, &params, sizeof(params));
		const std::uint32_t zero{};
		queue.WriteBuffer(emitter->m_spawnCounterBuffer, 0, &zero, sizeof(zero));
		const DrawArguments args{};
		queue.WriteBuffer(emitter->m_drawArgumentsBuffer, 0, &args, sizeof(args));
		m_frameEmitters.push_back(std::move(emitter));
	}

	if (m_frameEmitters.empty())
		return;

	wgpu::ComputePassDescriptor passDesc{};
	passDesc.label = "ParticleSimulationAndCulling";
	auto pass{ encoder.BeginComputePass(&passDesc) };
	pass.SetPipeline(m_computePipeline);
	for (const auto& emitter : m_frameEmitters)
	{
		pass.SetBindGroup(0, emitter->m_computeBindGroup);
		pass.DispatchWorkgroups((emitter->m_config.capacity + s_workgroup_size - 1) / s_workgroup_size);
		emitter->m_clearRequested = false;
		emitter->m_sinceLastCompute = 0.0;
	}
	pass.End();
}

void ax::ParticleSystem::draw(wgpu::RenderPassEncoder& pass)
{
	if (m_thread != std::this_thread::get_id())
		throw std::runtime_error("Particle systems belong to the renderer thread");
	if (m_renderPipeline.Get() == nullptr)
		return;
	pass.SetPipeline(m_renderPipeline);
	pass.SetBindGroup(0, m_emptyBindGroup);
	for (const auto& emitter : m_frameEmitters)
	{
		if (!emitter->m_config.visible)
			continue;
		pass.SetBindGroup(1, emitter->m_renderBindGroup);
		pass.DrawIndirect(emitter->m_drawArgumentsBuffer, 0);
	}
}

void ax::ParticleSystem::shutdown()
{
	for (const auto& weakEmitter : m_emitters)
	{
		if (const auto emitter{ weakEmitter.lock() })
		{
			emitter->m_particleBuffer = nullptr;
			emitter->m_uniformBuffer = nullptr;
			emitter->m_spawnCounterBuffer = nullptr;
			emitter->m_visibleBuffer = nullptr;
			emitter->m_drawArgumentsBuffer = nullptr;
			emitter->m_computeBindGroup = nullptr;
			emitter->m_renderBindGroup = nullptr;
		}
	}
	m_frameEmitters.clear();
	m_emitters.clear();
	m_renderPipeline = nullptr;
	m_computePipeline = nullptr;
	m_renderPipelineLayout = nullptr;
	m_computePipelineLayout = nullptr;
	m_emptyBindGroup = nullptr;
	m_renderLayout = nullptr;
	m_emptyLayout = nullptr;
	m_computeLayout = nullptr;
	m_device = nullptr;
	m_colourFormat = wgpu::TextureFormat::Undefined;
	m_sampleCount = 0;
}
