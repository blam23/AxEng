#include "axeng/core/materials/texture_filter.h"

#include "axeng/core/texture.h"
#include "axeng/debug/gpu_memory.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <tuple>

namespace
{
	constexpr const char* s_prelude{ R"WGSL(
struct FilterParams {
	values: array<vec4<f32>, 16>,
	// The outputs' size in pixels.
	size: vec4<f32>,
};
struct FilterQuad {
	rect: vec4<f32>,
	data: vec4<f32>,
};
struct FilterInput {
	pixel: vec2<i32>,
	uv: vec2<f32>,
	rect: vec4<f32>,
	data: vec4<f32>,
};

@group(0) @binding(0) var<uniform> params: FilterParams;
@group(0) @binding(1) var<storage, read> quads: array<FilterQuad>;
@group(0) @binding(2) var input0: texture_2d<f32>;
@group(0) @binding(3) var input1: texture_2d<f32>;
@group(0) @binding(4) var input2: texture_2d<f32>;
@group(0) @binding(5) var input3: texture_2d<f32>;

fn filter_load(t: texture_2d<f32>, texel: vec2<i32>) -> vec4<f32> {
	let size = vec2<i32>(textureDimensions(t, 0));
	return textureLoad(t, clamp(texel, vec2<i32>(0), size - vec2<i32>(1)), 0);
}
fn load0(texel: vec2<i32>) -> vec4<f32> { return filter_load(input0, texel); }
fn load1(texel: vec2<i32>) -> vec4<f32> { return filter_load(input1, texel); }
fn load2(texel: vec2<i32>) -> vec4<f32> { return filter_load(input2, texel); }
fn load3(texel: vec2<i32>) -> vec4<f32> { return filter_load(input3, texel); }

struct FilterVertex {
	@builtin(position) position: vec4<f32>,
	@location(0) uv: vec2<f32>,
	@location(1) @interpolate(flat) quad: u32,
};

@vertex
fn filter_vertex(@builtin(vertex_index) vertex: u32, @builtin(instance_index) instance: u32) -> FilterVertex {
	var corners = array<vec2<f32>, 6>(vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 0.0), vec2<f32>(0.0, 1.0),
		vec2<f32>(1.0, 0.0), vec2<f32>(1.0, 1.0), vec2<f32>(0.0, 1.0));
	let corner = corners[vertex];
	let rect = quads[instance].rect;
	let pixel = rect.xy + corner * rect.zw;
	var out: FilterVertex;
	out.position = vec4<f32>(pixel.x / params.size.x * 2.0 - 1.0, 1.0 - pixel.y / params.size.y * 2.0, 0.0, 1.0);
	out.uv = corner;
	out.quad = instance;
	return out;
}
)WGSL" };

	struct Pipeline
	{
		wgpu::RenderPipeline pipeline;
	};

	struct State
	{
		wgpu::Device device;
		wgpu::BindGroupLayout layout;
		wgpu::PipelineLayout pipelineLayout;
		ax::gpu::TrackedTexture blank;
		wgpu::TextureView blankView;
		// By source and output formats.
		std::map<std::tuple<std::size_t, wgpu::TextureFormat, wgpu::TextureFormat>, wgpu::RenderPipeline> pipelines;
	};

	std::unique_ptr<State> s_state{};

	State* state_for(const wgpu::Device& device)
	{
		if (s_state && s_state->device.Get() == device.Get())
			return s_state.get();
		auto state{ std::make_unique<State>() };
		state->device = device;

		std::array<wgpu::BindGroupLayoutEntry, 2 + ax::TextureFilter::inputCount> entries{};
		entries[0].binding = 0;
		entries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
		entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
		entries[1].binding = 1;
		entries[1].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
		entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
		for (std::uint32_t i{}; i < ax::TextureFilter::inputCount; ++i)
		{
			auto& entry{ entries[2 + i] };
			entry.binding = 2 + i;
			entry.visibility = wgpu::ShaderStage::Fragment;
			entry.texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
			entry.texture.viewDimension = wgpu::TextureViewDimension::e2D;
		}
		wgpu::BindGroupLayoutDescriptor layoutDesc{};
		layoutDesc.label = "Texture filter";
		layoutDesc.entryCount = entries.size();
		layoutDesc.entries = entries.data();
		state->layout = device.CreateBindGroupLayout(&layoutDesc);

		wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{};
		pipelineLayoutDesc.bindGroupLayoutCount = 1;
		pipelineLayoutDesc.bindGroupLayouts = &state->layout;
		state->pipelineLayout = device.CreatePipelineLayout(&pipelineLayoutDesc);

		wgpu::TextureDescriptor blankDesc{};
		blankDesc.label = "Texture filter blank input";
		blankDesc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
		blankDesc.size = { 1, 1, 1 };
		blankDesc.format = wgpu::TextureFormat::RGBA8Unorm;
		state->blank = ax::gpu::create_texture(device, blankDesc, "Texture filter");
		const std::array<std::uint8_t, 4> zero{};
		wgpu::TexelCopyTextureInfo copy{};
		copy.texture = state->blank;
		wgpu::TexelCopyBufferLayout copyLayout{};
		copyLayout.bytesPerRow = 4;
		copyLayout.rowsPerImage = 1;
		const wgpu::Extent3D extent{ 1, 1, 1 };
		device.GetQueue().WriteTexture(&copy, zero.data(), zero.size(), &copyLayout, &extent);
		state->blankView = state->blank.CreateView();

		s_state = std::move(state);
		return s_state.get();
	}

	wgpu::RenderPipeline pipeline_for(State& state, const ax::TextureFilterPass& pass)
	{
		const auto first{ pass.outputs[0]->texture().GetFormat() };
		const auto second{ pass.outputs.size() > 1 ? pass.outputs[1]->texture().GetFormat() : wgpu::TextureFormat::Undefined };
		const auto key{ std::tuple{ std::hash<std::string>{}(pass.fragment), first, second } };
		if (const auto found{ state.pipelines.find(key) }; found != state.pipelines.end())
			return found->second;

		std::string code{ s_prelude };
		code += pass.outputs.size() > 1
			? "struct FilterOutput {\n\t@location(0) out0: vec4<f32>,\n\t@location(1) out1: vec4<f32>,\n};\n"
			: "struct FilterOutput {\n\t@location(0) out0: vec4<f32>,\n};\n";
		code += pass.fragment;
		code += R"WGSL(
@fragment
fn filter_fragment(in_: FilterVertex) -> FilterOutput {
	var info: FilterInput;
	info.pixel = vec2<i32>(floor(in_.position.xy));
	info.uv = in_.uv;
	info.rect = quads[in_.quad].rect;
	info.data = quads[in_.quad].data;
	return filter_texel(info);
}
)WGSL";
		wgpu::ShaderSourceWGSL source{};
		source.code = { code.data(), code.size() };
		wgpu::ShaderModuleDescriptor shaderDesc{};
		shaderDesc.nextInChain = &source;
		shaderDesc.label = pass.label.empty() ? "Texture filter" : pass.label.c_str();
		const auto shader{ state.device.CreateShaderModule(&shaderDesc) };

		std::array<wgpu::ColorTargetState, ax::TextureFilter::outputCount> targets{};
		for (std::size_t i{}; i < pass.outputs.size(); ++i)
		{
			targets[i].format = pass.outputs[i]->texture().GetFormat();
			targets[i].writeMask = wgpu::ColorWriteMask::All;
		}
		wgpu::FragmentState fragment{};
		fragment.module = shader;
		fragment.entryPoint = "filter_fragment";
		fragment.targetCount = pass.outputs.size();
		fragment.targets = targets.data();

		wgpu::RenderPipelineDescriptor desc{};
		desc.label = shaderDesc.label;
		desc.layout = state.pipelineLayout;
		desc.vertex.module = shader;
		desc.vertex.entryPoint = "filter_vertex";
		desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
		desc.multisample.count = 1;
		desc.multisample.mask = ~0u;
		desc.fragment = &fragment;
		auto pipeline{ state.device.CreateRenderPipeline(&desc) };
		state.pipelines.emplace(key, pipeline);
		return pipeline;
	}
}

bool ax::TextureFilter::run(const wgpu::Device& device, const wgpu::Queue& queue, const TextureFilterPass& pass)
{
	if (device == nullptr || pass.fragment.empty() || pass.outputs.empty() || pass.outputs.size() > outputCount
		|| pass.inputs.size() > inputCount || pass.params.size() > parameterCount)
	{
		spdlog::error("Texture filter '{}': invalid pass", pass.label);
		return false;
	}
	const auto width{ pass.outputs[0] ? pass.outputs[0]->width() : 0u };
	const auto height{ pass.outputs[0] ? pass.outputs[0]->height() : 0u };
	for (const auto* output : pass.outputs)
		if (!output || output->texture().Get() == nullptr || output->width() != width || output->height() != height)
		{
			spdlog::error("Texture filter '{}': outputs must be render targets of one size", pass.label);
			return false;
		}
	if (pass.quads.empty())
		return true;

	auto& state{ *state_for(device) };
	const auto pipeline{ pipeline_for(state, pass) };
	if (pipeline.Get() == nullptr)
		return false;

	struct Params
	{
		std::array<glm::vec4, parameterCount> values{};
		glm::vec4 size{};
	} params{};
	std::copy(pass.params.begin(), pass.params.end(), params.values.begin());
	params.size = { static_cast<float>(width), static_cast<float>(height), 0.0f, 0.0f };

	wgpu::BufferDescriptor paramsDesc{};
	paramsDesc.label = "Texture filter parameters";
	paramsDesc.size = sizeof(Params);
	paramsDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform;
	const auto paramsBuffer{ gpu::create_buffer(device, paramsDesc, "Texture filter") };
	queue.WriteBuffer(paramsBuffer, 0, &params, sizeof(params));

	wgpu::BufferDescriptor quadsDesc{};
	quadsDesc.label = "Texture filter quads";
	quadsDesc.size = pass.quads.size() * sizeof(TextureFilterQuad);
	quadsDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Storage;
	const auto quadsBuffer{ gpu::create_buffer(device, quadsDesc, "Texture filter") };
	queue.WriteBuffer(quadsBuffer, 0, pass.quads.data(), quadsDesc.size);

	std::array<wgpu::BindGroupEntry, 2 + inputCount> entries{};
	entries[0].binding = 0;
	entries[0].buffer = paramsBuffer;
	entries[0].size = sizeof(Params);
	entries[1].binding = 1;
	entries[1].buffer = quadsBuffer;
	entries[1].size = quadsDesc.size;
	for (std::uint32_t i{}; i < inputCount; ++i)
	{
		entries[2 + i].binding = 2 + i;
		const auto* input{ i < pass.inputs.size() ? pass.inputs[i] : nullptr };
		entries[2 + i].textureView = input && input->view().Get() ? input->view() : state.blankView;
	}
	wgpu::BindGroupDescriptor groupDesc{};
	groupDesc.layout = state.layout;
	groupDesc.entryCount = entries.size();
	groupDesc.entries = entries.data();
	const auto group{ device.CreateBindGroup(&groupDesc) };

	wgpu::CommandEncoderDescriptor encoderDesc{};
	encoderDesc.label = "Texture filter";
	auto encoder{ device.CreateCommandEncoder(&encoderDesc) };
	std::array<wgpu::RenderPassColorAttachment, outputCount> attachments{};
	for (std::size_t i{}; i < pass.outputs.size(); ++i)
	{
		attachments[i].view = pass.outputs[i]->view();
		attachments[i].depthSlice = wgpu::kDepthSliceUndefined;
		attachments[i].loadOp = wgpu::LoadOp::Load;
		attachments[i].storeOp = wgpu::StoreOp::Store;
	}
	wgpu::RenderPassDescriptor passDesc{};
	passDesc.colorAttachmentCount = pass.outputs.size();
	passDesc.colorAttachments = attachments.data();
	auto renderPass{ encoder.BeginRenderPass(&passDesc) };
	renderPass.SetPipeline(pipeline);
	renderPass.SetBindGroup(0, group);
	renderPass.Draw(6, static_cast<std::uint32_t>(pass.quads.size()));
	renderPass.End();
	auto command{ encoder.Finish() };
	queue.Submit(1, &command);
	return true;
}

void ax::TextureFilter::shutdown()
{
	s_state.reset();
}
