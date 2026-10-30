#pragma once

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <vector>

namespace ax
{
	// A copy of the frame so far with a chain of ever blurrier, half size levels, for materials that look through
	// themselves at what is behind them (frosted glass). Level 0 is the frame itself.
	class BackdropBlur
	{
	public:
		// `levels` is how many blurred levels follow the sharp one (fewer for a very small frame).
		bool init(const wgpu::Device& device, wgpu::TextureFormat format, std::uint32_t levels);

		// Copies the frame and builds the chain. A multisampled frame is resolved from `multisampled`; otherwise it is
		// copied from `frame`, which must allow copying from it. Returns false if neither is possible.
		bool encode(const wgpu::CommandEncoder& encoder, const wgpu::TextureView& multisampled,
			const wgpu::Texture& frame, std::uint32_t width, std::uint32_t height);

		// The whole chain, for sampling at fractional levels. Empty until encode has succeeded.
		const wgpu::TextureView& view() const noexcept { return m_chainView; }

	private:
		bool resize(std::uint32_t width, std::uint32_t height);

		wgpu::Device m_device;
		wgpu::TextureFormat m_format{ wgpu::TextureFormat::Undefined };
		std::uint32_t m_levels{};
		std::uint32_t m_width{}, m_height{};
		wgpu::Texture m_texture;
		wgpu::TextureView m_chainView;
		std::vector<wgpu::TextureView> m_levelViews;
		std::vector<wgpu::BindGroup> m_downsampleGroups;
		wgpu::BindGroupLayout m_layout;
		wgpu::RenderPipeline m_pipeline;
		wgpu::Sampler m_sampler;
	};
}
