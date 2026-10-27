#include "axeng/core/materials/material_renderer.h"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <map>
#include <memory>
#include <utility>

namespace
{
	// The stage every material shares. The material's own source follows it, then the fragment entry point.
	constexpr const char* s_prelude = R"WGSL(
struct Globals {
	viewport : vec4<f32>,
	camera : vec4<f32>,
	time : vec4<f32>,
};

@group(0) @binding(0) var<uniform> globals : Globals;
@group(0) @binding(1) var<uniform> params : array<vec4<f32>, 128>;

struct VSIn {
	@builtin(vertex_index) corner : u32,
	@location(0) rect : vec4<f32>,
	@location(1) extra : vec4<f32>,
	@location(2) data0 : vec4<f32>,
	@location(3) data1 : vec4<f32>,
};

struct VSOut {
	@builtin(position) pos : vec4<f32>,
	@location(0) world : vec2<f32>,
	@location(1) uv : vec2<f32>,
	@location(2) @interpolate(flat) data0 : vec4<f32>,
	@location(3) @interpolate(flat) data1 : vec4<f32>,
	@location(4) @interpolate(flat) size : vec2<f32>,
};

struct MaterialInput {
	world : vec2<f32>,
	uv : vec2<f32>,
	size : vec2<f32>,
	data0 : vec4<f32>,
	data1 : vec4<f32>,
	time : f32,
	zoom : f32,
};

@vertex
fn vs_main(in_ : VSIn) -> VSOut {
	var corners = array<vec2<f32>, 6>(
		vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 0.0), vec2<f32>(0.0, 1.0),
		vec2<f32>(0.0, 1.0), vec2<f32>(1.0, 0.0), vec2<f32>(1.0, 1.0));
	let corner = corners[in_.corner];
	let world = in_.rect.xy + corner * in_.rect.zw;
	// The same transform and depth mapping as the shape renderer, so materials interleave with shapes by z.
	let screenPos = select(
		(world - globals.camera.xy) * globals.camera.z + globals.viewport.xy * 0.5,
		world,
		globals.camera.w > 0.5);
	let ndcX = (screenPos.x / globals.viewport.x) * 2.0 - 1.0;
	let ndcY = 1.0 - (screenPos.y / globals.viewport.y) * 2.0;
	let depth = 0.5 + atan(in_.extra.x) / 3.141592653589793;

	var out : VSOut;
	out.pos = vec4<f32>(ndcX, ndcY, depth, 1.0);
	out.world = world;
	out.uv = corner;
	out.data0 = in_.data0;
	out.data1 = in_.data1;
	out.size = in_.rect.zw;
	return out;
}
)WGSL";

	constexpr const char* s_epilogue = R"WGSL(
@fragment
fn fs_main(in_ : VSOut) -> @location(0) vec4<f32> {
	var m : MaterialInput;
	m.world = in_.world;
	m.uv = in_.uv;
	m.size = in_.size;
	m.data0 = in_.data0;
	m.data1 = in_.data1;
	m.time = globals.time.x;
	m.zoom = select(globals.camera.z, 1.0, globals.camera.w > 0.5);
	return material(m);
}
)WGSL";

	struct QuadVertex
	{
		glm::vec4 rect;
		glm::vec4 extra;
		glm::vec4 data0;
		glm::vec4 data1;
	};
	static_assert(sizeof(QuadVertex) == 64);

	// One uniform slot per draw: 256 bytes for the globals, then the parameters. Dynamic offsets need 256 alignment.
	constexpr std::uint64_t s_globalsStride{ 256 };
	constexpr std::uint64_t s_paramBytes{ ax::MaterialRenderer::parameterCount * sizeof(glm::vec4) };
	constexpr std::uint64_t s_slotStride{ s_globalsStride + s_paramBytes };
	constexpr std::uint64_t s_slotCount{ 512 };
	constexpr std::uint64_t s_quadArenaSize{ 8ull * 1024 * 1024 };

	struct Entry
	{
		ax::MaterialDefinition definition;
		wgpu::RenderPipeline pipeline;
		bool built{ false };
		std::uint64_t generation{ 0 };
	};

	struct State
	{
		wgpu::Device device;
		wgpu::Queue queue;
		ax::MaterialRendererConfig config;
		wgpu::BindGroupLayout groupLayout;
		wgpu::PipelineLayout pipelineLayout;
		wgpu::Buffer uniforms;
		wgpu::BindGroup bindGroup;
		wgpu::Buffer quads;
		std::uint64_t slot{ 0 };
		std::uint64_t quadOffset{ 0 };
		ax::MaterialView view{};
		bool hasView{ false };
		std::uint32_t skipped{ 0 };
		std::uint32_t lastSkipped{ 0 };
		std::uint64_t generation{ 1 };
		bool warnedFull{ false };
	};

	std::map<std::string, Entry>& definitions()
	{
		static std::map<std::string, Entry> map{};
		return map;
	}

	std::unique_ptr<State> s_state{};
	std::uint64_t s_definitionGeneration{ 1 };

	wgpu::RenderPipeline build_pipeline(State& state, const ax::MaterialDefinition& definition)
	{
		const auto source{ std::string{ s_prelude } + "\n" + definition.fragment + "\n" + s_epilogue };
		wgpu::ShaderSourceWGSL wgsl{};
		wgsl.code = { source.data(), source.size() };
		wgpu::ShaderModuleDescriptor shaderDesc{};
		shaderDesc.nextInChain = &wgsl;
		shaderDesc.label = definition.name.c_str();
		const auto shader{ state.device.CreateShaderModule(&shaderDesc) };

		wgpu::VertexAttribute attributes[4]{};
		for (std::uint32_t i = 0; i < 4; ++i)
			attributes[i] = { .format = wgpu::VertexFormat::Float32x4, .offset = i * sizeof(glm::vec4), .shaderLocation = i };
		wgpu::VertexBufferLayout vertexLayout{};
		vertexLayout.stepMode = wgpu::VertexStepMode::Instance;
		vertexLayout.arrayStride = sizeof(QuadVertex);
		vertexLayout.attributeCount = 4;
		vertexLayout.attributes = attributes;

		wgpu::BlendState blend{};
		blend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
		blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
		blend.color.operation = wgpu::BlendOperation::Add;
		blend.alpha.srcFactor = wgpu::BlendFactor::One;
		blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
		blend.alpha.operation = wgpu::BlendOperation::Add;

		wgpu::ColorTargetState colourTarget{};
		colourTarget.format = state.config.colourFormat;
		colourTarget.blend = &blend;
		colourTarget.writeMask = wgpu::ColorWriteMask::All;

		wgpu::FragmentState fragment{};
		fragment.module = shader;
		fragment.entryPoint = "fs_main";
		fragment.targetCount = 1;
		fragment.targets = &colourTarget;

		// Reverse depth like the shapes and sprites. Never written, so the same pipeline also draws into a pass
		// whose depth attachment is read only.
		wgpu::DepthStencilState depth{};
		depth.format = state.config.depthFormat;
		depth.depthWriteEnabled = wgpu::OptionalBool::False;
		depth.depthCompare = wgpu::CompareFunction::GreaterEqual;

		wgpu::RenderPipelineDescriptor desc{};
		desc.label = definition.name.c_str();
		desc.layout = state.pipelineLayout;
		desc.vertex.module = shader;
		desc.vertex.entryPoint = "vs_main";
		desc.vertex.bufferCount = 1;
		desc.vertex.buffers = &vertexLayout;
		desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
		desc.primitive.cullMode = wgpu::CullMode::None;
		desc.depthStencil = state.config.depthFormat != wgpu::TextureFormat::Undefined ? &depth : nullptr;
		desc.multisample.count = state.config.sampleCount;
		desc.multisample.mask = ~0u;
		desc.fragment = &fragment;
		return state.device.CreateRenderPipeline(&desc);
	}
}

bool ax::MaterialRenderer::define(MaterialDefinition definition)
{
	if (definition.name.empty() || definition.fragment.empty())
		return false;
	auto name{ definition.name };
	auto& entry{ definitions()[name] };
	entry.definition = std::move(definition);
	entry.pipeline = {};
	entry.built = false;
	entry.generation = ++s_definitionGeneration;
	return true;
}

bool ax::MaterialRenderer::is_defined(const std::string& name)
{
	return definitions().contains(name);
}

bool ax::MaterialRenderer::is_unlit(const std::string& name)
{
	const auto found{ definitions().find(name) };
	return found != definitions().end() && found->second.definition.unlit;
}

bool ax::MaterialRenderer::init(const wgpu::Device& device, const MaterialRendererConfig& config)
{
	if (device == nullptr || config.colourFormat == wgpu::TextureFormat::Undefined)
	{
		spdlog::error("MaterialRenderer: init requires a device and a colour format");
		return false;
	}
	if (s_state && s_state->device.Get() == device.Get() && s_state->config == config)
		return true;

	auto state{ std::make_unique<State>() };
	state->device = device;
	state->queue = device.GetQueue();
	state->config = config;

	wgpu::BindGroupLayoutEntry entries[2]{};
	for (std::uint32_t i = 0; i < 2; ++i)
	{
		entries[i].binding = i;
		entries[i].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
		entries[i].buffer.type = wgpu::BufferBindingType::Uniform;
		entries[i].buffer.hasDynamicOffset = true;
	}
	entries[0].buffer.minBindingSize = 3 * sizeof(glm::vec4);
	entries[1].buffer.minBindingSize = s_paramBytes;
	wgpu::BindGroupLayoutDescriptor groupDesc{};
	groupDesc.entryCount = 2;
	groupDesc.entries = entries;
	state->groupLayout = device.CreateBindGroupLayout(&groupDesc);

	wgpu::PipelineLayoutDescriptor layoutDesc{};
	layoutDesc.bindGroupLayoutCount = 1;
	layoutDesc.bindGroupLayouts = &state->groupLayout;
	state->pipelineLayout = device.CreatePipelineLayout(&layoutDesc);

	wgpu::BufferDescriptor uniformDesc{};
	uniformDesc.label = "MaterialUniforms";
	uniformDesc.size = s_slotStride * s_slotCount;
	uniformDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform;
	state->uniforms = device.CreateBuffer(&uniformDesc);

	wgpu::BindGroupEntry bindings[2]{};
	bindings[0].binding = 0;
	bindings[0].buffer = state->uniforms;
	bindings[0].offset = 0;
	bindings[0].size = 3 * sizeof(glm::vec4);
	bindings[1].binding = 1;
	bindings[1].buffer = state->uniforms;
	bindings[1].offset = 0;
	bindings[1].size = s_paramBytes;
	wgpu::BindGroupDescriptor bindDesc{};
	bindDesc.layout = state->groupLayout;
	bindDesc.entryCount = 2;
	bindDesc.entries = bindings;
	state->bindGroup = device.CreateBindGroup(&bindDesc);

	wgpu::BufferDescriptor quadDesc{};
	quadDesc.label = "MaterialQuads";
	quadDesc.size = s_quadArenaSize;
	quadDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Vertex;
	state->quads = device.CreateBuffer(&quadDesc);

	state->generation = s_state ? s_state->generation + 1 : 1;
	if (s_state)
	{
		state->view = s_state->view;
		state->hasView = s_state->hasView;
	}
	for (auto& [_, entry] : definitions())
	{
		entry.pipeline = {};
		entry.built = false;
	}
	s_state = std::move(state);
	if (s_state->hasView)
		begin_frame(s_state->view);
	return true;
}

void ax::MaterialRenderer::shutdown()
{
	for (auto& [_, entry] : definitions())
	{
		entry.pipeline = {};
		entry.built = false;
	}
	s_state.reset();
}

bool ax::MaterialRenderer::is_initialised()
{
	return s_state != nullptr;
}

void ax::MaterialRenderer::begin_frame(const MaterialView& view)
{
	if (!s_state)
		return;
	s_state->view = view;
	s_state->hasView = true;
	s_state->slot = 0;
	s_state->quadOffset = 0;
	s_state->lastSkipped = s_state->skipped;
	s_state->skipped = 0;
}

std::uint32_t ax::MaterialRenderer::last_skipped()
{
	return s_state ? s_state->lastSkipped : 0;
}

void ax::MaterialRenderer::draw(wgpu::RenderPassEncoder& pass, const MaterialDraw& draw)
{
	if (!s_state || !s_state->hasView || draw.quads.empty())
		return;
	auto& state{ *s_state };
	const auto found{ definitions().find(draw.material) };
	if (found == definitions().end())
	{
		++state.skipped;
		static bool warned{ false };
		if (!std::exchange(warned, true))
			spdlog::error("MaterialRenderer: material '{}' is not defined", draw.material);
		return;
	}
	auto& entry{ found->second };
	if (!entry.built)
	{
		entry.pipeline = build_pipeline(state, entry.definition);
		entry.built = true;
	}
	if (entry.pipeline == nullptr)
	{
		++state.skipped;
		return;
	}

	const std::uint64_t quadBytes{ draw.quads.size() * sizeof(QuadVertex) };
	if (state.slot >= s_slotCount || state.quadOffset + quadBytes > s_quadArenaSize)
	{
		++state.skipped;
		if (!std::exchange(state.warnedFull, true))
			spdlog::error("MaterialRenderer: out of space for material '{}' ({} quads)", draw.material, draw.quads.size());
		return;
	}

	const auto& view{ state.view };
	const float globals[12]{
		view.viewport.x, view.viewport.y, 0.0f, 0.0f,
		view.cameraPosition.x, view.cameraPosition.y, view.zoom, draw.screenSpace ? 1.0f : 0.0f,
		static_cast<float>(view.time), 0.0f, 0.0f, 0.0f,
	};
	const std::uint64_t globalsOffset{ state.slot * s_slotStride };
	const std::uint64_t paramsOffset{ globalsOffset + s_globalsStride };
	++state.slot;
	state.queue.WriteBuffer(state.uniforms, globalsOffset, globals, sizeof(globals));
	glm::vec4 params[MaterialRenderer::parameterCount]{};
	const auto count{ std::min(draw.params.size(), MaterialRenderer::parameterCount) };
	for (std::size_t i = 0; i < count; ++i)
		params[i] = draw.params[i];
	state.queue.WriteBuffer(state.uniforms, paramsOffset, params, sizeof(params));

	std::vector<QuadVertex> data(draw.quads.size());
	for (std::size_t i = 0; i < draw.quads.size(); ++i)
	{
		const auto& quad{ draw.quads[i] };
		data[i] = { { quad.position, quad.size }, { quad.z, 0.0f, 0.0f, 0.0f }, quad.data0, quad.data1 };
	}
	const auto quadOffset{ state.quadOffset };
	state.quadOffset += quadBytes;
	state.queue.WriteBuffer(state.quads, quadOffset, data.data(), quadBytes);

	const std::uint32_t offsets[2]{ static_cast<std::uint32_t>(globalsOffset), static_cast<std::uint32_t>(paramsOffset) };
	pass.SetPipeline(entry.pipeline);
	pass.SetBindGroup(0, state.bindGroup, 2, offsets);
	pass.SetVertexBuffer(0, state.quads, quadOffset, quadBytes);
	pass.Draw(6, static_cast<std::uint32_t>(draw.quads.size()));
}
