#include "compute_program.h"

#include <array>
#include <vector>

bool ax::ComputeProgram::initialise(const wgpu::Device& device, std::string_view source,
	std::span<const wgpu::BindGroupLayoutEntry> bindings,
	std::span<const std::string_view> entryPoints, std::string_view label)
{
	clear();
	if (!device || source.empty() || entryPoints.empty()) return false;

	wgpu::ShaderSourceWGSL wgsl{};
	wgsl.code = { source.data(), source.size() };
	wgpu::ShaderModuleDescriptor shaderDescriptor{};
	shaderDescriptor.nextInChain = &wgsl;
	shaderDescriptor.label = label;
	const auto shader{ device.CreateShaderModule(&shaderDescriptor) };
	if (!shader) return false;

	wgpu::BindGroupLayoutDescriptor bindGroupLayoutDescriptor{};
	bindGroupLayoutDescriptor.entryCount = static_cast<std::uint32_t>(bindings.size());
	bindGroupLayoutDescriptor.entries = bindings.data();
	bindGroupLayoutDescriptor.label = label;
	m_bindGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDescriptor);
	if (!m_bindGroupLayout) return false;

	wgpu::PipelineLayoutDescriptor pipelineLayoutDescriptor{};
	pipelineLayoutDescriptor.bindGroupLayoutCount = 1;
	pipelineLayoutDescriptor.bindGroupLayouts = &m_bindGroupLayout;
	pipelineLayoutDescriptor.label = label;
	m_pipelineLayout = device.CreatePipelineLayout(&pipelineLayoutDescriptor);
	if (!m_pipelineLayout) return false;

	for (const auto entryPoint : entryPoints)
	{
		wgpu::ComputePipelineDescriptor pipelineDescriptor{};
		pipelineDescriptor.layout = m_pipelineLayout;
		pipelineDescriptor.compute.module = shader;
		pipelineDescriptor.compute.entryPoint = entryPoint;
		pipelineDescriptor.label = label;
		auto pipeline{ device.CreateComputePipeline(&pipelineDescriptor) };
		if (!pipeline)
		{
			clear();
			return false;
		}
		m_pipelines.emplace(entryPoint, std::move(pipeline));
	}
	return true;
}

void ax::ComputeProgram::clear() noexcept
{
	m_pipelines.clear();
	m_pipelineLayout = nullptr;
	m_bindGroupLayout = nullptr;
}

wgpu::BindGroup ax::ComputeProgram::create_bind_group(const wgpu::Device& device,
	std::span<const wgpu::BindGroupEntry> entries, std::string_view label) const
{
	if (!m_bindGroupLayout) return {};
	wgpu::BindGroupDescriptor descriptor{};
	descriptor.layout = m_bindGroupLayout;
	descriptor.entryCount = static_cast<std::uint32_t>(entries.size());
	descriptor.entries = entries.data();
	descriptor.label = label;
	return device.CreateBindGroup(&descriptor);
}

bool ax::ComputeProgram::encode(wgpu::CommandEncoder& encoder, std::string_view entryPoint,
	const wgpu::BindGroup& bindGroup, std::uint32_t x, std::uint32_t y, std::uint32_t z) const
{
	const std::array<ComputeDispatch, 1> dispatches{ ComputeDispatch{ entryPoint, bindGroup, x, y, z } };
	return encode_many(encoder, dispatches);
}

bool ax::ComputeProgram::encode_many(wgpu::CommandEncoder& encoder,
	std::span<const ComputeDispatch> dispatches) const
{
	if (dispatches.empty()) return false;
	for (const auto& dispatch : dispatches)
		if (!m_pipelines.contains(std::string{ dispatch.entryPoint }) || !dispatch.bindGroup
			|| dispatch.x == 0 || dispatch.y == 0 || dispatch.z == 0) return false;
	wgpu::ComputePassDescriptor descriptor{};
	auto pass{ encoder.BeginComputePass(&descriptor) };
	for (const auto& dispatch : dispatches)
	{
		pass.SetPipeline(m_pipelines.at(std::string{ dispatch.entryPoint }));
		pass.SetBindGroup(0, dispatch.bindGroup);
		pass.DispatchWorkgroups(dispatch.x, dispatch.y, dispatch.z);
	}
	pass.End();
	return true;
}
