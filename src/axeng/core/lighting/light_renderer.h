#pragma once

#include "axeng/debug/gpu_memory.h"
#include <webgpu/webgpu_cpp.h>
#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "axeng/core/texture.h"
#include "axeng/core/lighting/radiance_cascades.h"
#include "axeng/core/lighting/ambient_occlusion.h"

namespace ax
{
	// The light map is a single sample target of this format.
	constexpr wgpu::TextureFormat light_map_format{ wgpu::TextureFormat::RGBA16Float };

	// A light in world pixels. It adds colour * intensity (a negative intensity subtracts light), fading to nothing at radius with
	// (1 - d^2)^falloff. A cone limits it to directions within coneOuter of direction, fully lit within
	// coneInner (both cosines of half angles); coneOuter <= -1 lights every direction.
	// A length above 0 makes it a beam instead: a line from position along direction (a unit vector) for length pixels,
	// lit the same along its whole length and fading out to radius on every side of the line, d being the distance to
	// it. A beam has no cone.
	// With radiance cascades on (LightingSettings::radiance), a light with shadows above 0 is blocked by occluders, light
	// leaving a disc of sourceRadius pixels round its position (0 for the smallest the grid can resolve). shadows 1
	// traces it through the cascades, cheap however many lights there are; shadows 2 (marched_shadows) draws it plainly
	// and marches each pixel's way to it through the cascades' distance field instead, for the few lights that move
	// (a player's): the cascades take a light's shape at probes on a fixed grid, so a moving one's edges would crawl.
	struct Light2D
	{
		glm::vec2 position{};
		float radius{ 64.0f };
		float intensity{ 1.0f };
		glm::vec3 colour{ 1.0f };
		float falloff{ 2.0f };
		glm::vec2 direction{ 1.0f, 0.0f };
		float coneInner{ -1.0f };
		float coneOuter{ -1.0f };
		float length{ 0.0f };
		float sourceRadius{ 0.0f };
		float shadows{ 1.0f };
		// Set by the renderer, 0 otherwise: drawn only out to nearReach pixels, fading out from sourceRadius, as the
		// analytic near part of a light traced through the radiance cascades (which fade it in over the same span).
		float nearReach{ 0.0f };

		static constexpr std::size_t gpuDataSize{ 64 };
		static constexpr float marched_shadows{ 2.0f };
	};

	// A soft additive halo in world pixels, drawn over the lit scene so it stays bright in the dark.
	struct Glow2D
	{
		glm::vec2 position{};
		float radius{ 16.0f };
		float intensity{ 1.0f };
		glm::vec4 colour{ 1.0f };

		static constexpr std::size_t gpuDataSize{ 32 };
	};

	// A convex quad in world pixels that resets the light map to ambient, hiding every light drawn before it. Lights
	// from firstLight on are drawn after it. Used for things standing over the world, such as moving decks, so the
	// lights beneath them stay hidden. Corners go round the quad in order. A non-zero layer names the occluder, so
	// lights drawn elsewhere (particles) can be placed just after it.
	struct LightOccluder2D
	{
		std::array<glm::vec2, 4> corners{};
		std::uint32_t firstLight{};
		std::uint32_t layer{};
	};

	static_assert(offsetof(Light2D, position) == 0);
	static_assert(offsetof(Light2D, radius) == 8);
	static_assert(offsetof(Light2D, intensity) == 12);
	static_assert(offsetof(Light2D, colour) == 16);
	static_assert(offsetof(Light2D, falloff) == 28);
	static_assert(offsetof(Light2D, direction) == 32);
	static_assert(offsetof(Light2D, coneInner) == 40);
	static_assert(offsetof(Light2D, coneOuter) == 44);
	static_assert(offsetof(Light2D, length) == 48);
	static_assert(offsetof(Light2D, sourceRadius) == 52);
	static_assert(sizeof(Light2D) == Light2D::gpuDataSize);
	static_assert(offsetof(Glow2D, colour) == 16);
	static_assert(sizeof(Glow2D) == Glow2D::gpuDataSize);

	// A drifting, thresholded noise mask that darkens the light map over world space.
	struct NoiseShadow2D
	{
		glm::vec2 scale{ 320.0f };
		glm::vec2 offset{};
		float coverage{ 0.48f };
		float softness{ 0.16f };
		float darkness{ 0.18f };
		float seed{ 17.0f };
	};

	// How the light map becomes what multiplies the scene. There is no HDR scene buffer (the target is multiplied by the
	// light in place), so the curve shapes the light, not the lit colour: the light is scaled by exposure, its
	// saturation set, then curved into 0..1 (`white` reaching 1), raised to gamma and scaled by maxExposure.
	// The default (clamp, everything 1) is min(light, maxExposure), the light as it always was.
	struct Tonemap
	{
		enum class Curve : std::uint32_t
		{
			clamp, // light / maxExposure, clipped (white unused)
			reinhard, // extended Reinhard: x (1 + x / white^2) / (1 + x)
			aces, // Narkowicz's fit of the ACES curve, scaled so white reaches 1
			filmic, // Hable's (Uncharted 2) curve, scaled so white reaches 1
			count
		};
		Curve curve{ Curve::clamp };
		float exposure{ 1.0f };
		// The light that comes out at full brightness (for the curves; at least 0.01).
		float white{ 1.0f };
		// 0 grey, 1 as is, above 1 more coloured.
		float saturation{ 1.0f };
		// Applied to the curved 0..1 light: above 1 darkens the mid tones, below 1 lifts them.
		float gamma{ 1.0f };
		// Curves the brightest channel and scales the others with it, so bright coloured light keeps its hue rather than
		// washing out to white.
		bool preserveHue{};

		bool operator==(const Tonemap&) const = default;
	};

	// The tonemapped light (what fs_apply multiplies the target by, before ambient occlusion), as the shader does it.
	glm::vec3 tonemap_light(const Tonemap&, glm::vec3 light, float maxExposure) noexcept;

	struct LightingSettings
	{
		// When disabled, or when ambient is full white with nothing to light, frames render exactly as without
		// lighting.
		bool enabled{ false };
		// The light everything lit receives before any Light2D is added.
		glm::vec3 ambient{ 1.0f };
		// The light map is the target size divided by this (1, 2 or 4).
		std::uint32_t resolutionDivisor{ 2 };
		// Total light is clamped to this (at most 2), so lights brighten a fully lit scene by at most this much.
		float maxExposure{ 1.0f };
		// The curve the light goes through before it multiplies the scene.
		Tonemap tonemap{};
		// Debug: draw only the light (the light map, then glows) in place of the scene instead of lighting it.
		bool lightOnly{};
		// False leaves out the lights of particle emitters (their own and each particle's), as when zoomed out too far to
		// see them.
		bool particleLights{ true };
		// Shadows from radiance cascades (radiance_cascades.h).
		RadianceSettings radiance{};

		bool operator==(const LightingSettings&) const = default;
	};

	// Whether a frame with these settings and lights needs the lighting passes at all.
	bool lighting_active(const LightingSettings&, std::size_t lights, std::size_t glows, bool hasShadows = false) noexcept;

	// Accumulates every light into a reduced resolution light map in one instanced draw (plus one per occluder),
	// multiplies the target by it in one fullscreen pass and adds glows in one more instanced draw.
	class LightRenderer
	{
	public:
		// Creates the pipelines for drawing onto targets of this format and sample count. Calling again with the
		// same arguments is a no-op.
		bool init(const wgpu::Device&, wgpu::TextureFormat targetFormat, std::uint32_t sampleCount);
		bool initialised() const noexcept { return m_lightPipeline != nullptr; }

		// Records the light map, apply and glow passes onto target, which is width x height. extraLights, if given,
		// draws more additive lights into the light map pass: once with layer 0 beneath every occluder, and once just
		// after each occluder with its layer. Occluders are in firstLight order.
		void encode(const wgpu::Device&, const wgpu::Queue&, wgpu::CommandEncoder&, const wgpu::TextureView& target,
			std::uint32_t width, std::uint32_t height, const LightView&, const LightingSettings&,
			std::span<const Light2D>, std::span<const Glow2D>,
		const std::function<void(wgpu::RenderPassEncoder&, std::uint32_t layer)>& extraLights = {},
			const NoiseShadow2D* noiseShadow = nullptr,
			std::span<const LightOccluder2D> occluders = {}, std::span<const RadianceCircle> radianceCircles = {},
			const RadianceCascades::OccluderDraw& radianceOccluders = {}, std::uint64_t radianceOccluderSignature = 0,
			const AmbientOcclusion2D* ambientOcclusion = nullptr, const AmbientOcclusion::CasterDraw& aoCasters = {});

		struct Stats
		{
			std::uint32_t lights{}, glows{}, lightMapWidth{}, lightMapHeight{}, occluders{};
			// The radiance cascades grid (0 when they did not run), its cascades and the lights traced through it (also
			// counted in lights).
			std::uint32_t radianceWidth{}, radianceHeight{}, radianceCascades{}, radianceLights{}, radianceCircles{};
			// The cascades were not traced this frame: nothing they read had changed, the last result was reused.
			bool radianceReused{};
			// Ambient occlusion (AmbientOcclusion2D) darkened the light map.
			bool ambientOcclusion{};
		};
		Stats last_stats() const noexcept { return m_stats; }
		// GPU time per stage of a recent frame's lighting (empty without timestamp queries); also Tracy plots.
		const std::vector<GpuPassTimer::Timing>& gpu_timings() const noexcept { return m_timer.last(); }
		// Call once the command buffer encode recorded into has been submitted, for gpu_timings to come back.
		void submitted() { m_timer.submitted(); }
		// The light map the last encode drew (empty before the first), for materials that read it.
		wgpu::TextureView light_map_view() const { return m_lightMap ? m_lightMap->view() : wgpu::TextureView{}; }
	private:
		struct InstanceBuffer
		{
			gpu::TrackedBuffer buffer{};
			std::size_t capacity{};
			wgpu::BindGroup bindGroup{};
		};
		bool ensure_light_map(const wgpu::Device&, std::uint32_t width, std::uint32_t height);
		void ensure_buffer(const wgpu::Device&, InstanceBuffer&, std::size_t bytes, const char* label);

		wgpu::Device m_device{};
		wgpu::TextureFormat m_targetFormat{ wgpu::TextureFormat::Undefined };
		std::uint32_t m_sampleCount{};
		wgpu::RenderPipeline m_lightPipeline{}, m_shadowPipeline{}, m_occluderPipeline{}, m_applyPipeline{}, m_brightApplyPipeline{}, m_glowPipeline{},
			m_lightOnlyPipeline{};
		wgpu::BindGroupLayout m_instanceLayout{}, m_applyLayout{}, m_marchLayout{};
		gpu::TrackedBuffer m_uniforms{}, m_marchUniforms{};
		wgpu::BindGroup m_marchGroup{};
		InstanceBuffer m_lights{}, m_glows{}, m_occluders{};
		std::unique_ptr<Texture> m_lightMap{};
		wgpu::Sampler m_sampler{};
		wgpu::BindGroup m_applyBindGroup{};
		// White, the ambient occlusion factor without it, and the factor the apply bind group was made with.
		gpu::TrackedTexture m_whiteFactor{};
		wgpu::TextureView m_whiteFactorView{}, m_applyFactorView{};
		void make_apply_bind_group(const wgpu::Device&, const wgpu::TextureView& factor);
		std::vector<glm::vec2> m_occluderScratch{};
		// Two, so that when the grid's density changes (at each half octave of zoom) the old one's last result fades out
		// over a few frames rather than the traced light jumping.
		std::array<RadianceCascades, 2> m_cascades{};
		AmbientOcclusion m_ambientOcclusion{};
		GpuPassTimer m_timer{};
		std::size_t m_active{};
		float m_lastTexelsPerPixel{};
		std::uint32_t m_fadeLeft{};
		RadianceCascades& active_cascades() noexcept { return m_cascades[m_active]; }
		std::vector<Light2D> m_tracedLights{}, m_analyticLights{}, m_nearLights{};
		std::vector<LightOccluder2D> m_shiftedOccluders{};
		LightView m_previousView{};
		std::uint32_t m_previousDivisor{ 1 };
		bool m_lightMapDrawn{};
		Stats m_stats{};
	};
}
