#pragma once

#include "axeng/core/lighting/radiance_cascades.h"
#include "axeng/debug/gpu_memory.h"
#include "axeng/debug/gpu_pass_timer.h"

#include <webgpu/webgpu_cpp.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace ax
{
	// Something that stirs the mist, in world pixels: within radius (fading out to it) the mist is pulled towards
	// velocity * push plus outward pixels a second away from position, and cleared by clear a second (1 clears it all
	// in a second). Cosmetic: the stirring lives on the GPU only.
	struct MistMover
	{
		glm::vec2 position{};
		glm::vec2 velocity{};
		float radius{ 16.0f };
		float push{ 1.0f };
		float outward{ 0.0f };
		float clear{ 0.0f };

		static constexpr std::size_t gpuDataSize{ 32 };
	};
	static_assert(sizeof(MistMover) == MistMover::gpuDataSize);

	// Where the mist forms first (0 not especially, 1 most) over a world-aligned grid, such as wet ground. Changing
	// version uploads it again.
	struct MistMask
	{
		glm::vec2 origin{};
		float texel{ 32.0f };
		std::uint32_t width{}, height{};
		std::vector<std::uint8_t> values{};
		std::uint64_t version{};
	};

	// Mist over the lit scene: drifting, domain-warped noise thresholded by amount, thicker where its mask is,
	// stirred by MistMovers on a low-resolution fluid grid fixed in the world, lit by the light map (with halos of
	// scattered light round bright lights) and thinner over things that stand up (sprite_flag_ao_caster).
	struct Mist2D
	{
		// How much mist: 0 none (nothing is drawn), 1 all the noise can give.
		float amount{};
		// Seconds the mist has drifted for, and since the last frame (for the stirring).
		float time{};
		float deltaSeconds{ 1.0f / 60.0f };
		// The mist's colour in full white light (its albedo times the sky's tint) and its opacity where densest.
		glm::vec3 colour{ 0.88f, 0.9f, 0.94f };
		float opacity{ 0.85f };
		// World pixels across the noise's largest features, how far it is warped (in those) and its seed.
		float scale{ 384.0f };
		float warp{ 0.6f };
		float seed{ 3.0f };
		// The noise threshold at full amount (lower covers more) and the softness of the edges.
		float coverage{ 0.42f };
		float softness{ 0.2f };
		// How much the mask lowers the threshold, so mist shows there first.
		float maskBias{ 0.35f };
		// World pixels a second the mist drifts.
		glm::vec2 wind{ 6.0f, 2.0f };
		// Towards the sun (x east, y south, z up) and how much brighter the mist's sides facing it get.
		glm::vec3 toSun{ 0.0f, 0.0f, 1.0f };
		float sunStrength{ 0.0f };
		// Scattered light round bright lights: its strength and reach in world pixels (0 strength: none).
		float haloStrength{ 0.6f };
		float haloRadius{ 48.0f };
		// How much thinner mist is over things that stand up (0 not at all, 1 none there).
		float tallThinning{ 0.7f };
		// Stirring: on, world pixels per grid texel, seconds a cleared hole takes to fill, how fast the stirred air
		// calms (per second) and how far (in seconds of its motion) it drags the mist's pattern.
		bool stir{ true };
		float gridTexel{ 8.0f };
		float refillSeconds{ 4.0f };
		float damping{ 1.2f };
		float displace{ 0.25f };
		// Screen pixels per texel of the mist's density (1 to 8).
		float resolution{ 2.0f };
		// Debug: 1 density, 2 stirring (velocity), 3 cleared, 4 mask, 5 the tall mask; drawn opaque.
		std::uint32_t debugView{};

		bool operator==(const Mist2D&) const = default;
	};

	// One frame's mist for LightRenderer::encode.
	struct MistFrame
	{
		Mist2D mist{};
		std::span<const MistMover> movers{};
		const MistMask* mask{};
	};

	// Whether the mist draws anything.
	bool mist_visible(const Mist2D&) noexcept;

	// The stirring grid for a view: world-aligned, covering the view plus a margin, its texel the mist's gridTexel
	// (doubled until it fits in 512 x 512), its size rounded up to whole 64 texel blocks so small zooms keep it.
	struct MistGrid
	{
		glm::vec2 origin{};
		float texel{ 8.0f };
		std::uint32_t width{}, height{};
	};
	MistGrid mist_grid(const LightView&, float gridTexel) noexcept;

	class MistRenderer
	{
	public:
		// Draws the silhouettes of things that stand up into the pass for this view (AmbientOcclusion::CasterDraw's
		// slot 1).
		using TallDraw = std::function<void(wgpu::RenderPassEncoder&, const LightView&)>;
		// Where the tall things' silhouettes come from: a mask covering the screen plus margin screen pixels each side
		// (the ambient occlusion's), or none (the mist draws its own with tallDraw).
		struct TallMask
		{
			wgpu::TextureView view{};
			float margin{};
		};

		bool init(const wgpu::Device&);
		// The composite pass's group 1 layout (lighting.wgsl's fs_mist).
		const wgpu::BindGroupLayout& composite_layout() const noexcept { return m_compositeLayout; }
		// Stirs, draws the density and the halos for this frame. False: nothing to draw.
		bool encode(const wgpu::Device&, const wgpu::Queue&, wgpu::CommandEncoder&, const LightView& screen,
			const MistFrame&, const wgpu::TextureView& lightMap, std::uint32_t mapWidth, std::uint32_t mapHeight,
			std::uint32_t divisor, const TallMask& tall, const TallDraw& tallDraw, GpuPassTimer* timer = nullptr);
		// The composite's bindings, after encode returned true.
		const wgpu::BindGroup& composite_group() const noexcept { return m_compositeGroup; }

		struct Stats
		{
			std::uint32_t gridWidth{}, gridHeight{}, movers{};
			// The stirring started over this frame (a new grid).
			bool reset{};
		};
		Stats last_stats() const noexcept { return m_stats; }
		const MistGrid& grid() const noexcept { return m_grid; }

	private:
		struct Target
		{
			gpu::TrackedTexture texture{};
			wgpu::TextureView view{};
			std::uint32_t width{}, height{};
		};
		Target make(const wgpu::Device&, const char* label, std::uint32_t width, std::uint32_t height,
			wgpu::TextureFormat format) const;
		wgpu::BindGroup bind(const wgpu::Device&, const gpu::TrackedBuffer& uniforms, const wgpu::TextureView& a,
			const wgpu::TextureView& b, const wgpu::TextureView& c) const;
		void upload_mask(const wgpu::Device&, const wgpu::Queue&, const MistMask&);

		wgpu::RenderPipeline m_splatPipeline{}, m_advectPipeline{}, m_divergencePipeline{}, m_jacobiPipeline{},
			m_projectPipeline{}, m_densityPipeline{}, m_downsamplePipeline{}, m_blurPipeline{};
		wgpu::BindGroupLayout m_layout{}, m_compositeLayout{};
		wgpu::Sampler m_sampler{};
		// Uniforms: the stirring, the density, the two blur passes (the first also downsamples) and the composite.
		gpu::TrackedBuffer m_uniforms[4]{}, m_compositeUniforms{};
		gpu::TrackedBuffer m_movers{};
		std::size_t m_moverCapacity{};
		gpu::TrackedTexture m_dummy{};
		wgpu::TextureView m_dummyView{};
		// Stirring: the state (velocity, cleared) of this frame and the last, advected, the forces, divergence and
		// pressure.
		Target m_state[2]{}, m_advected{}, m_force{}, m_divergence{}, m_pressure[2]{};
		std::size_t m_current{};
		bool m_hasState{};
		MistGrid m_grid{};
		Target m_density{}, m_halo[2]{}, m_tall{};
		Target m_mask{};
		std::uint64_t m_maskVersion{ ~0ull };
		glm::vec4 m_maskRect{};
		wgpu::BindGroup m_compositeGroup{};
		Stats m_stats{};
	};
}
