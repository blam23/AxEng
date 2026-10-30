#pragma once

#include <webgpu/webgpu_cpp.h>

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ax
{
	struct ComputeDispatch
	{
		std::string_view entryPoint{};
		wgpu::BindGroup bindGroup{};
		std::uint32_t x{}, y{ 1 }, z{ 1 };
	};

	// A WGSL compute shader with shared bindings and one or more entry points.
	class ComputeProgram
	{
	public:
		bool initialise(const wgpu::Device& device, std::string_view source,
			std::span<const wgpu::BindGroupLayoutEntry> bindings,
			std::span<const std::string_view> entryPoints, std::string_view label = {});
		void clear() noexcept;
		bool ready() const noexcept { return m_pipelineLayout.Get() != nullptr; }

		wgpu::BindGroup create_bind_group(const wgpu::Device& device,
			std::span<const wgpu::BindGroupEntry> entries, std::string_view label = {}) const;
		bool encode(wgpu::CommandEncoder& encoder, std::string_view entryPoint,
			const wgpu::BindGroup& bindGroup, std::uint32_t x, std::uint32_t y = 1,
			std::uint32_t z = 1) const;
	bool encode_many(wgpu::CommandEncoder& encoder, std::span<const ComputeDispatch> dispatches) const;

	private:
		wgpu::BindGroupLayout m_bindGroupLayout{};
		wgpu::PipelineLayout m_pipelineLayout{};
		std::unordered_map<std::string, wgpu::ComputePipeline> m_pipelines{};
	};
}
