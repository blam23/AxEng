#pragma once

#include "axeng/core/lighting/light_renderer.h"

#include <webgpu/webgpu_cpp.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace ax
{
	enum class ParticleSpawnShape : std::uint32_t
	{
		Point,
		Circle,
		Rectangle,
	};

	// value + amplitude * sin(2 * pi * frequency * t + phase), with t the seconds the emitter has existed.
	struct ParticleWave
	{
		float amplitude{};
		float frequency{ 1.0f };
		float phase{};

		bool operator==(const ParticleWave&) const = default;
	};

	// A light at the emitter. It is on while the emitter plays and fades out over the longest particle lifetime after
	// it stops. The waves modulate it over time: the radius and intensity by adding to them, the colour by blending
	// from colour towards colourB by (0.5 + 0.5 * wave) * amplitude.
	struct ParticleLightConfig
	{
		bool enabled{ false };
		glm::vec3 colour{ 1.0f, 0.8f, 0.5f };
		glm::vec3 colourB{ 1.0f };
		float radius{ 96.0f };
		float intensity{ 1.0f };
		float falloff{ 2.0f };
		glm::vec2 offset{};
		ParticleWave radiusWave{};
		ParticleWave intensityWave{};
		ParticleWave colourWave{};
	};

	// Every live particle is a light of its own: its colour, with alpha scaling the intensity, and a radius of its
	// current size times radiusScale.
	struct ParticleEmissionLightConfig
	{
		bool enabled{ false };
		float radiusScale{ 8.0f };
		float intensity{ 1.0f };
		float falloff{ 2.0f };
	};

	struct ParticleConfig
	{
		std::uint32_t capacity{ 1024 };
		float emissionRate{ 20.0f };
		float duration{ 1.0f };
		float explosiveness{};
		bool loop{ false };
		bool playing{ false };
		bool visible{ true };
		std::uint32_t seed{ 1 };
		glm::vec2 position{};
		float rotation{};
		bool localSpace{ false };
		ParticleSpawnShape spawnShape{ ParticleSpawnShape::Circle };
		glm::vec2 spawnSize{};
		float direction{};
		float spread{ 6.2831853f };
		float spawnRadius{};
		float particleRotationMin{};
		float particleRotationMax{ 6.2831853f };
		float angularVelocityMin{ -6.2831853f };
		float angularVelocityMax{ 6.2831853f };
		float lifetimeMin{ 0.5f };
		float lifetimeMax{ 1.0f };
		float speedMin{ 10.0f };
		float speedMax{ 30.0f };
		float sizeStart{ 4.0f };
		float sizeEnd{ 1.0f };
		glm::vec4 colorStart{ 1.0f };
		glm::vec4 colorEnd{ 1.0f, 1.0f, 1.0f, 0.0f };
		glm::vec2 gravity{};
		float z{};
		ParticleLightConfig light{};
		ParticleEmissionLightConfig particleLight{};
	};

	class ParticleEmitter
	{
	public:
		const ParticleConfig& config() const noexcept { return m_config; }
		void set_config(const ParticleConfig& config);
		void set_position(glm::vec2 position);
		void set_rotation(float rotation);
		// Recolours the emitter, its live particles included (colours are applied as particles are drawn).
		void set_colors(glm::vec4 start, glm::vec4 end);
		void play();
		void stop() noexcept;
		void clear() noexcept;
		void set_visible(bool visible) noexcept;
		bool finished() const noexcept;
		void check_thread() const;

	private:
		explicit ParticleEmitter(ParticleConfig config);
		std::uint32_t advance(double delta);
		void update_world_bounds();
		// The emitter's own light this frame, if it is on.
		bool static_light(Light2D& light) const;
		float particle_light_reach() const noexcept;

		ParticleConfig m_config;
		std::thread::id m_thread{ std::this_thread::get_id() };
		double m_elapsed{};
		double m_systemTime{};
		double m_sinceLastCompute{};
		double m_lightTime{};
		double m_lightTail{};
		double m_emissionRemainder{};
		std::uint32_t m_spawnCount{};
		std::uint32_t m_frame{};
		glm::vec4 m_worldBounds{};
		bool m_hasWorldBounds{};
		bool m_clearRequested{ true };
		bool m_suspended{};
		bool m_resumePlaying{};
		bool m_resumeVisible{};
		wgpu::Buffer m_particleBuffer;
		wgpu::Buffer m_uniformBuffer;
		wgpu::Buffer m_spawnCounterBuffer;
		wgpu::Buffer m_visibleBuffer;
		wgpu::Buffer m_drawArgumentsBuffer;
		wgpu::BindGroup m_computeBindGroup;
		wgpu::BindGroup m_renderBindGroup;

		friend class ParticleSystem;
	};

	class ParticleSystem
	{
	public:
		ParticleSystem() = default;
		ParticleSystem(const ParticleSystem&) = delete;
		ParticleSystem& operator=(const ParticleSystem&) = delete;

		std::shared_ptr<ParticleEmitter> create(const ParticleConfig& config);
		void set_paused(bool paused);
		void set_render_enabled(bool enabled);
		void suspend_all();
		void resume_all();
		void encode_compute(wgpu::Device& device, wgpu::Queue& queue, wgpu::CommandEncoder& encoder,
			double delta, glm::vec4 cameraBounds, glm::vec2 viewport, float zoom,
			wgpu::TextureFormat colourFormat, std::uint32_t sampleCount);
		void draw(wgpu::RenderPassEncoder& pass);
		// Emitter lights as of the last compute pass; submit them with the frame's other lights.
		std::span<const Light2D> lights() const noexcept { return m_frameLights; }
		// How many emitters will draw per-particle lights in draw_lights.
		std::size_t emission_light_emitters() const noexcept { return m_emissionLightEmitters; }
		// Adds every live particle's light to the light map, which is an RGBA16F single sample target.
		void draw_lights(wgpu::RenderPassEncoder& pass);
		void shutdown();

	private:
		void initialise(wgpu::Device& device, wgpu::TextureFormat colourFormat, std::uint32_t sampleCount);

		std::thread::id m_thread{ std::this_thread::get_id() };
		wgpu::Device m_device;
		wgpu::ComputePipeline m_computePipeline;
		wgpu::RenderPipeline m_renderPipeline;
		wgpu::RenderPipeline m_lightPipeline;
		wgpu::BindGroupLayout m_computeLayout;
		wgpu::BindGroupLayout m_emptyLayout;
		wgpu::BindGroupLayout m_renderLayout;
		wgpu::BindGroup m_emptyBindGroup;
		wgpu::PipelineLayout m_computePipelineLayout;
		wgpu::PipelineLayout m_renderPipelineLayout;
		std::vector<std::weak_ptr<ParticleEmitter>> m_emitters;
		std::vector<std::shared_ptr<ParticleEmitter>> m_frameEmitters;
		std::vector<Light2D> m_frameLights;
		std::size_t m_emissionLightEmitters{};
		bool m_paused{};
		bool m_render_enabled{ true };
		wgpu::TextureFormat m_colourFormat{ wgpu::TextureFormat::Undefined };
		std::uint32_t m_sampleCount{};
	};

	std::optional<std::string> validate_particle_config(const ParticleConfig& config);
}
