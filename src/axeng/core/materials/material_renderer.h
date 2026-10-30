#pragma once

#include <webgpu/webgpu_cpp.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ax
{
	// A material is a named WGSL fragment shader. The renderer wraps it in a vertex stage that draws instanced quads,
	// a view/time uniform and a block of 128 vec4 parameters, so a material can be anything from animated water to a
	// distorting ice surface without the engine knowing what it is.
	//
	// The source defines
	//
	//     fn material(in: MaterialInput) -> vec4<f32>
	//
	// and may read `params[0..127]` (vec4<f32> each, set per draw) and `globals`. MaterialInput holds:
	//
	//     world  : vec2<f32>  the fragment's position in world pixels (screen pixels for a screen space draw)
	//     uv     : vec2<f32>  0..1 across the quad
	//     size   : vec2<f32>  the quad's size in pixels
	//     data0, data1 : vec4<f32>  the quad's own data, constant across it
	//     time   : f32        seconds
	//     zoom   : f32        pixels on screen per world pixel
	//
	// The result is straight alpha blended over what is already drawn.
	//
	// A `backdrop` material also gets
	//
	//     fn backdrop(screen: vec2<f32>, lod: f32) -> vec3<f32>
	//
	// which reads a copy of everything drawn before the interface, in screen pixels, at a blur level from 0 (sharp)
	// up to MaterialRenderer's backdrop level count (each level halves the resolution).
	struct MaterialDefinition
	{
		std::string name;
		std::string fragment;
		// Drawn after lighting, so the lights do not darken it (for something that glows).
		bool unlit{ false };
		// Reads the backdrop (see above). Only screen space draws make sense for it: the window draws them first in
		// its interface pass, ahead of the interface's other shapes, and they write depth so what is covered stays
		// covered. Drawn without a backdrop it is skipped.
		bool backdrop{ false };
	};

	struct MaterialQuad
	{
		glm::vec2 position{ 0.0f };
		glm::vec2 size{ 1.0f };
		float z{ 0.0f };
		glm::vec4 data0{ 0.0f };
		glm::vec4 data1{ 0.0f };
	};

	struct MaterialDraw
	{
		std::string material;
		std::vector<MaterialQuad> quads;
		// Up to MaterialRenderer::parameterCount entries; the rest are zero.
		std::vector<glm::vec4> params;
		// Positions are screen pixels and the camera is ignored.
		bool screenSpace{ false };
	};

	struct MaterialRendererConfig
	{
		wgpu::TextureFormat colourFormat{ wgpu::TextureFormat::Undefined };
		wgpu::TextureFormat depthFormat{ wgpu::TextureFormat::Depth24Plus };
		std::uint32_t sampleCount{ 4 };

		bool operator==(const MaterialRendererConfig&) const = default;
	};

	struct MaterialView
	{
		glm::vec2 viewport{ 0.0f };
		glm::vec2 cameraPosition{ 0.0f };
		float zoom{ 1.0f };
		double time{ 0.0 };
	};

	class MaterialRenderer
	{
	public:
		static constexpr std::size_t parameterCount{ 128 };
		// The sharpest blur level is 0, the blurriest this many halvings down.
		static constexpr std::uint32_t backdropLevels{ 5 };

		// Adds or replaces a material. Returns false for an empty name or source. A source the GPU rejects is
		// reported when the material is first drawn (and the draw is skipped).
		static bool define(MaterialDefinition definition);
		static bool is_defined(const std::string& name);
		static bool is_unlit(const std::string& name);
		static bool is_backdrop(const std::string& name);

		// The mip chain `backdrop()` reads, for this frame's draws. Pass an empty view to have them skipped.
		static void set_backdrop(const wgpu::TextureView& view);

		// Creates the arenas and layouts. Calling again with the same device and config is a no-op; a new config
		// rebuilds the pipelines on next use.
		static bool init(const wgpu::Device& device, const MaterialRendererConfig& config);
		static void shutdown();
		static bool is_initialised();

		// Sets the view and time for this frame and recycles the upload arenas. Must only be called once the
		// command buffer holding the previous frame's draws has been submitted.
		static void begin_frame(const MaterialView& view);

		// Draws inside a pass whose colour, depth and sample count match the config. Depth is tested (z is mapped like
		// the shape renderer's, so materials interleave with shapes and sprites) but never written.
		static void draw(wgpu::RenderPassEncoder& pass, const MaterialDraw& draw);

		// Draws that the last frame skipped (unknown material, bad shader, arena full).
		static std::uint32_t last_skipped();
	};
}
