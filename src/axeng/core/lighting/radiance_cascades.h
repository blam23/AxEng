#pragma once

#include "axeng/debug/gpu_memory.h"
#include <webgpu/webgpu_cpp.h>
#include <glm/glm.hpp>

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
		std::uint32_t divisor{ 2 };
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
	RadianceGrid radiance_grid(const LightView&, const RadianceSettings&, float reach) noexcept;

	// Whether the light goes through the cascades on this grid: one that casts shadows, is positive, omni or a cone (not
	// a beam), large enough and inside the grid.
	bool radiance_traces(const Light2D&, const RadianceGrid&, const RadianceSettings&) noexcept;

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
		void encode(const wgpu::Device&, const wgpu::Queue&, wgpu::CommandEncoder&, const RadianceGrid&,
			const RadianceSettings&, const LightView& view, std::uint32_t lightMapDivisor, glm::vec3 ambient,
			std::span<const Light2D> lights, std::span<const RadianceCircle> circles, const OccluderDraw& occluders,
			const Previous& previous);
		// Replaces the light map with ambient (scaled by sky visibility) plus the traced light. Only after encode.
		void draw_resolve(wgpu::RenderPassEncoder&) const;
		bool ready() const noexcept { return m_ready; }

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
		wgpu::BindGroup bind(const wgpu::Device&, const wgpu::TextureView& a, const wgpu::TextureView& b,
			const wgpu::TextureView& uintTexture, const wgpu::TextureView& filtered, const wgpu::TextureView& e,
			const wgpu::Buffer& storage) const;

		wgpu::Device m_device{};
		wgpu::TextureFormat m_lightMapFormat{ wgpu::TextureFormat::Undefined };
		wgpu::BindGroupLayout m_layout{};
		wgpu::RenderPipeline m_circlePipeline{}, m_emitterPipeline{}, m_seedPipeline{}, m_jumpPipeline{},
			m_distancePipeline{}, m_cascadePipeline{}, m_resolvePipeline{};
		gpu::TrackedBuffer m_uniforms{}, m_lights{}, m_circles{};
		std::size_t m_lightCapacity{}, m_circleCapacity{};
		wgpu::Sampler m_sampler{};
		Target m_scene{}, m_jump[2]{}, m_distance{}, m_cascade[2]{};
		Target m_dummyFloat{}, m_dummyUint{};
		std::uint32_t m_width{}, m_height{}, m_probeSpacing{};
		wgpu::BindGroup m_resolveGroup{};
		std::uint32_t m_resolveOffset{};
		std::vector<float> m_lightScratch{};
		bool m_ready{};
		Stats m_stats{};
	};
}
