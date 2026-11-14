#pragma once

#include "axeng/debug/gpu_memory.h"
#include "axeng/debug/gpu_pass_timer.h"
#include <webgpu/webgpu_cpp.h>
#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace ax
{
	struct Light2D;

	// The camera lights are drawn for: the viewport in screen pixels, the world position at its centre and screen pixels
	// per world pixel.
	struct LightView
	{
		glm::vec2 viewport{ 0.0f };
		glm::vec2 cameraPosition{ 0.0f };
		float zoom{ 1.0f };
	};

	// The radiance cascades scene target. Occluder sprites drawn into the scene pass write their albedo with alpha 1
	// (alpha 0 is empty, 2 and above are traced lights).
	constexpr wgpu::TextureFormat radiance_scene_format{ wgpu::TextureFormat::RGBA16Float };
	// Half floats in the scene's alpha number the traced lights, so at most this many are traced.
	constexpr std::size_t radiance_max_lights{ 2046 };

	// 2D global illumination with radiance cascades: lights that may cast shadows are traced through a world-aligned
	// grid around the view, so every occluder blocks them with soft, correct penumbrae. See docs/lighting.md.
	struct RadianceSettings
	{
		bool enabled{ false };
		// A GI texel is this many screen pixels (1 to 8).
		float divisor{ 2.0f };
		// Cascade 0's probes are this many GI texels apart (1 or 2).
		std::uint32_t probeSpacing{ 1 };
		// World pixels around the view that are traced too, so off-screen lights and occluders still count. Capped at
		// maxMarginFraction of the larger view side.
		float margin{ 320.0f };
		float maxMarginFraction{ 0.5f };
		// Ambient light is scaled by mix(1, sky visibility, aoStrength), sky visibility being the share of directions
		// with no occluder within aoRange world pixels.
		float aoStrength{ 0.0f };
		float aoRange{ 48.0f };
		// Occluders hit by light reflect this much of last frame's light times their albedo.
		float bounce{ 0.0f };
		// Four rays per direction, one towards each of the next cascade's nearest probes, so light from small sources
		// carries on without rings (the "bilinear fix"). About four times the tracing.
		bool bilinearFix{ true };
		// The cascades, from the lowest, that use it (the top one never needs it). Fewer is cheaper, the higher cascades'
		// rays being the longest, but each one left out rings round lights at the distance it covers.
		std::uint32_t fixCascades{ 8 };
		// Lights smaller than this many GI texels in radius are drawn without shadows.
		float minLightTexels{ 6.0f };
		// 0 off, 1 scene (occluders and emitters), 2 distance field, 3 traced light only, 4 sky visibility.
		std::uint32_t debugView{ 0 };

		bool operator==(const RadianceSettings&) const = default;
	};

	// A round occluder in world pixels, for world objects that are not sprites.
	struct RadianceCircle
	{
		glm::vec2 centre{};
		float radius{ 8.0f };
		float reserved{};
		glm::vec4 albedo{ 0.5f, 0.5f, 0.5f, 1.0f };
	};
	static_assert(sizeof(RadianceCircle) == 32);

	// Where the GI grid lies this frame: texel (0, 0) starts at origin, and a world pixel is texelsPerPixel texels.
	struct RadianceGrid
	{
		glm::vec2 origin{};
		float texelsPerPixel{ 1.0f };
		std::uint32_t width{}, height{};
		std::uint32_t probeSpacing{ 1 };
		std::uint32_t cascades{};

		// The world rect the grid covers.
		glm::vec2 world_min() const noexcept { return origin; }
		glm::vec2 world_max() const noexcept
		{
			return origin + glm::vec2{ static_cast<float>(width), static_cast<float>(height) } / texelsPerPixel;
		}
		// The view the scene pass draws with: a LightView whose viewport is the grid in texels.
		LightView view() const noexcept;
	};

	// The grid for a view: the view plus the margin, snapped to the coarsest probe spacing so probes stay fixed in the
	// world while the camera moves, sized so the cascades reach reach world pixels.
	// texelsPerPixel above 0 uses that density instead of the one the view's zoom picks (still coarsened if too many).
	RadianceGrid radiance_grid(const LightView&, const RadianceSettings&, float reach, float texelsPerPixel = 0.0f) noexcept;

	// Whether the light goes through the cascades on this grid: one that casts shadows, is positive, omni or a cone (not
	// a beam), large enough and inside the grid.
	bool radiance_traces(const Light2D&, const RadianceGrid&, const RadianceSettings&) noexcept;

	// The analytic near part of a traced light (Light2D::nearReach): close to its source the cascades' few rays cannot
	// resolve it and would flicker as it moves, so it is drawn plainly there (without shadows) and the cascades fade in
	// beyond. Matches near_span in radiance.wgsl.
	Light2D radiance_near_part(const Light2D&, const RadianceGrid&) noexcept;
	// How far a traced light's near part reaches (in texels): this many times its source plus this many probe spacings.
	// As NEAR_SCALE and NEAR_SPACINGS in radiance.wgsl.
	inline constexpr float radiance_near_scale{ 4.0f };
	inline constexpr float radiance_near_spacings{ 4.0f };

	class RadianceCascades
	{
	public:
		bool init(const wgpu::Device&, wgpu::TextureFormat lightMapFormat);
		bool initialised() const noexcept { return m_cascadePipeline != nullptr; }

		// Draws extra occluders (sprites) into the scene pass, with the grid's view. It may change pipelines and
		// bind groups.
		using OccluderDraw = std::function<void(wgpu::RenderPassEncoder&, const LightView& gridView)>;

		struct Previous
		{
			// Last frame's light map and the view it was drawn for, for bounce light. Empty when there is none.
			wgpu::TextureView lightMap{};
			LightView view{};
			std::uint32_t divisor{ 1 };
		};

		// Traces the lights through the grid: scene, distance field and cascades. Afterwards draw_resolve writes the
		// result into a light map pass.
		// When nothing the cascades read has changed since the last encode (the grid, the lights, the circles and
		// occluderSignature, a hash of what occluders draws, 0 for unknown) it traces nothing and only points the last
		// result's resolve at the view: most frames, with moving lights marched rather than traced.
		void encode(const wgpu::Device&, const wgpu::Queue&, wgpu::CommandEncoder&, const RadianceGrid&,
			const RadianceSettings&, const LightView& view, std::uint32_t lightMapDivisor, glm::vec3 ambient,
			std::span<const Light2D> lights, std::span<const RadianceCircle> circles, const OccluderDraw& occluders,
			const Previous& previous, std::uint64_t occluderSignature = 0);
		// Whether the last encode reused the one before's result (traced nothing).
		bool reused() const noexcept { return m_reused; }
		// Replaces the light map with ambient (scaled by sky visibility) plus the traced light; with a weight below 1,
		// blends that far towards it instead (fading from another grid's result). Only after encode.
		void draw_resolve(wgpu::RenderPassEncoder&, float weight = 1.0f) const;
		// Points the last encode's resolve at another view without tracing again: its grid is fixed in the world, so its
		// light still holds while the lights stay put (fading out over a few frames after the grid's density changed).
		void refresh_resolve(const wgpu::Queue&, const LightView& view, std::uint32_t lightMapDivisor, glm::vec3 ambient,
			const RadianceSettings* settings = nullptr);
		bool ready() const noexcept { return m_ready; }
		// Times each encode's passes on the GPU (null: not timed).
		void set_timer(GpuPassTimer* timer) noexcept { m_timer = timer; }

		// What lights with marched shadows march through: the distance field and scene of the last encode, and where
		// the grid lies (origin xy, texels per world pixel, 1 when usable; width, height). Placeholders unless `current`
		// (the cascades ran this frame) and ready.
		struct MarchInputs
		{
			wgpu::TextureView distance{}, scene{};
			glm::vec4 grid{}, size{};
		};
		MarchInputs march_inputs(bool current) const noexcept;

		struct Stats
		{
			std::uint32_t width{}, height{}, cascades{}, lights{}, circles{};
		};
		Stats last_stats() const noexcept { return m_stats; }

	private:
		struct Target
		{
			gpu::TrackedTexture texture{};
			wgpu::TextureView view{};
		};
		bool ensure_targets(const wgpu::Device&, std::uint32_t width, std::uint32_t height, std::uint32_t probeSpacing);
		void ensure_storage(const wgpu::Device&, gpu::TrackedBuffer&, std::size_t& capacity, std::size_t bytes, const char* label);
		enum class Bodies { none, write, read };
		wgpu::BindGroup bind(const wgpu::Device&, const wgpu::TextureView& a, const wgpu::TextureView& b,
			const wgpu::TextureView& uintTexture, const wgpu::TextureView& filtered, const wgpu::TextureView& e,
			const wgpu::Buffer& storage, Bodies bodies = Bodies::none) const;

		wgpu::Device m_device{};
		wgpu::TextureFormat m_lightMapFormat{ wgpu::TextureFormat::Undefined };
		wgpu::BindGroupLayout m_layout{};
		wgpu::RenderPipeline m_circlePipeline{}, m_emitterPipeline{}, m_seedPipeline{}, m_jumpPipeline{},
			m_distancePipeline{}, m_cascadePipeline{}, m_resolvePipeline{}, m_resolveBlendPipeline{}, m_combinePipeline{}, m_stampPipeline{};
		gpu::TrackedBuffer m_uniforms{}, m_lights{}, m_circles{}, m_bodies{}, m_bodiesWriteStandIn{}, m_bodiesReadStandIn{};
		std::size_t m_lightCapacity{}, m_circleCapacity{};
		wgpu::Sampler m_sampler{};
		// Occluders and lights' discs are drawn apart and combined into the scene.
		Target m_occluders{}, m_discs{}, m_scene{}, m_jump[2]{}, m_distance{}, m_cascade[2]{};
		Target m_dummyFloat{}, m_dummyUint{};
		std::uint32_t m_width{}, m_height{}, m_probeSpacing{};
		wgpu::BindGroup m_resolveGroup{};
		std::uint32_t m_resolveOffset{};
		std::array<glm::vec4, 8> m_resolveUniforms{};
		std::vector<float> m_lightScratch{};
		std::vector<std::uint32_t> m_bodyScratch{};
		bool m_ready{};
		GpuPassTimer* m_timer{};
		std::uint64_t m_signature{};
		bool m_reused{};
		RadianceGrid m_grid{};
		Stats m_stats{};
	};
}
