#pragma once

#include "axeng/core/lighting/radiance_cascades.h"
#include "axeng/debug/gpu_memory.h"
#include "axeng/debug/gpu_pass_timer.h"

#include <webgpu/webgpu_cpp.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <functional>

namespace ax
{
	// The casters' silhouettes: coverage in red (the window's ambient occlusion caster pipeline draws into it).
	constexpr wgpu::TextureFormat ao_mask_format{ wgpu::TextureFormat::R8Unorm };
	// Soft shadows round things that stand up: the silhouettes of sprites with sprite_flag_ao_caster, shifted by offset
	// (the way their shadows fall), blurred and multiplied into the light map like a NoiseShadow2D, by day and by night.
	struct AmbientOcclusion2D
	{
		// World pixels the shadow lies from its caster.
		glm::vec2 offset{};
		// World pixels the shadow's edge spreads over (the blur's sigma is half of it).
		float radius{ 10.0f };
		// How dark a spot under a whole caster gets: 0 not at all, 1 black.
		float darkness{ 0.35f };
		// How much of that a caster's own silhouette takes: 0 none (it stands above its shadow), 1 all.
		float selfDarkness{ 0.0f };
		// Screen pixels per texel of the shadow mask (1 to 8): higher is cheaper and softer.
		float resolution{ 4.0f };

		bool operator==(const AmbientOcclusion2D&) const = default;
	};

	class AmbientOcclusion
	{
	public:
		// Draws the casters' silhouettes as the window's radiance occluder sprites do, for this view, into the pass. Slots
		// 0 and 1 are the sprites drawn as themselves, shifted (their shadow) and where they stand; 2 and 3 the same for
		// the stand-ins (sprite_flag_ao_only). Each slot needs a camera buffer of its own (all are written before the
		// frame's commands run).
		using CasterDraw = std::function<void(wgpu::RenderPassEncoder&, const LightView&, std::uint32_t slot)>;

		bool init(const wgpu::Device&);
		// Draws and blurs this frame's shadow mask for a view of the screen, and from it the factor the light is
		// multiplied by over a light map of mapWidth x mapHeight texels, each divisor screen pixels across (factor_view).
		// False draws nothing (no casters or nothing to darken).
		bool encode(const wgpu::Device&, const wgpu::Queue&, wgpu::CommandEncoder&, const LightView& screen,
			std::uint32_t divisor, std::uint32_t mapWidth, std::uint32_t mapHeight, const AmbientOcclusion2D&,
			const CasterDraw&, GpuPassTimer* timer = nullptr);
		// The light's factor encode drew (red: 1 lit, 0 black), at the light map's size.
		const wgpu::TextureView& factor_view() const noexcept { return m_factor.view; }

	private:
		struct Target
		{
			gpu::TrackedTexture texture{};
			wgpu::TextureView view{};
		};
		bool ensure_targets(const wgpu::Device&, std::uint32_t width, std::uint32_t height);
		wgpu::BindGroup bind(const wgpu::Device&, const gpu::TrackedBuffer& uniforms, const wgpu::TextureView& source,
			const wgpu::TextureView& standIns, const wgpu::TextureView& spriteSilhouettes,
			const wgpu::TextureView& standInSilhouettes) const;

		wgpu::RenderPipeline m_blurFirstPipeline{}, m_blurSecondPipeline{}, m_applyPipeline{};
		wgpu::BindGroupLayout m_layout{};
		wgpu::Sampler m_sampler{};
		gpu::TrackedBuffer m_uniforms[3]{};
		// Per set (sprites, stand-ins): the shifted silhouettes and those where they stand.
		Target m_mask[2]{}, m_silhouettes[2]{}, m_blur[2]{}, m_factor{};
		wgpu::BindGroup m_applyGroup{};
		std::uint32_t m_width{}, m_height{}, m_factorWidth{}, m_factorHeight{};
	};
}
