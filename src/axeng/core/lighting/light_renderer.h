#pragma once

#include <webgpu/webgpu_cpp.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace ax
{
	// A light in world pixels. It adds colour * intensity, fading to nothing at radius with
	// (1 - d^2)^falloff. A cone limits it to directions within coneOuter of direction, fully lit within
	// coneInner (both cosines of half angles); coneOuter <= -1 lights every direction.
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

		static constexpr std::size_t gpuDataSize{ 48 };
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

	static_assert(offsetof(Light2D, position) == 0);
	static_assert(offsetof(Light2D, radius) == 8);
	static_assert(offsetof(Light2D, intensity) == 12);
	static_assert(offsetof(Light2D, colour) == 16);
	static_assert(offsetof(Light2D, falloff) == 28);
	static_assert(offsetof(Light2D, direction) == 32);
	static_assert(offsetof(Light2D, coneInner) == 40);
	static_assert(offsetof(Light2D, coneOuter) == 44);
	static_assert(sizeof(Light2D) == Light2D::gpuDataSize);
	static_assert(offsetof(Glow2D, colour) == 16);
	static_assert(sizeof(Glow2D) == Glow2D::gpuDataSize);

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

		bool operator==(const LightingSettings&) const = default;
	};

	struct LightView
	{
		glm::vec2 viewport{ 0.0f };
		glm::vec2 cameraPosition{ 0.0f };
		float zoom{ 1.0f };
	};

	// Whether a frame with these settings and lights needs the lighting passes at all.
	bool lighting_active(const LightingSettings&, std::size_t lights, std::size_t glows) noexcept;

	// Accumulates every light into a reduced resolution light map in one instanced draw, multiplies the target by
	// it in one fullscreen pass and adds glows in one more instanced draw.
	class LightRenderer
	{
	public:
		// Creates the pipelines for drawing onto targets of this format and sample count. Calling again with the
		// same arguments is a no-op.
		bool init(const wgpu::Device&, wgpu::TextureFormat targetFormat, std::uint32_t sampleCount);
		bool initialised() const noexcept { return m_lightPipeline != nullptr; }

		// Records the light map, apply and glow passes onto target, which is width x height.
		void encode(const wgpu::Device&, const wgpu::Queue&, wgpu::CommandEncoder&, const wgpu::TextureView& target,
			std::uint32_t width, std::uint32_t height, const LightView&, const LightingSettings&,
			std::span<const Light2D>, std::span<const Glow2D>);

		struct Stats
		{
			std::uint32_t lights{}, glows{}, lightMapWidth{}, lightMapHeight{};
		};
		Stats last_stats() const noexcept { return m_stats; }

	private:
		struct InstanceBuffer
		{
			wgpu::Buffer buffer{};
			std::size_t capacity{};
			wgpu::BindGroup bindGroup{};
		};
		bool ensure_light_map(const wgpu::Device&, std::uint32_t width, std::uint32_t height);
		void ensure_buffer(const wgpu::Device&, InstanceBuffer&, std::size_t bytes, const char* label);

		wgpu::Device m_device{};
		wgpu::TextureFormat m_targetFormat{ wgpu::TextureFormat::Undefined };
		std::uint32_t m_sampleCount{};
		wgpu::RenderPipeline m_lightPipeline{}, m_applyPipeline{}, m_brightApplyPipeline{}, m_glowPipeline{};
		wgpu::BindGroupLayout m_instanceLayout{}, m_applyLayout{};
		wgpu::Buffer m_uniforms{};
		InstanceBuffer m_lights{}, m_glows{};
		wgpu::Texture m_lightMap{};
		wgpu::TextureView m_lightMapView{};
		wgpu::Sampler m_sampler{};
		wgpu::BindGroup m_applyBindGroup{};
		Stats m_stats{};
	};
}
