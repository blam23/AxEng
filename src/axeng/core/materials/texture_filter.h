#pragma once

#include <webgpu/webgpu_cpp.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace ax
{
	class Texture;

	// A rectangle of the outputs to run a filter over, in their pixels, with data of its own.
	struct TextureFilterQuad
	{
		glm::vec4 rect{}; // x, y, width, height
		glm::vec4 data{};
	};

	// Runs a WGSL fragment over rectangles of one or two render targets, reading up to four textures by texel: for
	// work on whole textures such as turning a height map into normals. The source defines
	//
	//     fn filter_texel(in: FilterInput) -> FilterOutput
	//
	// where FilterInput holds pixel (vec2<i32>, the output texel), uv (0..1 across the rectangle), rect and data (the
	// quad's), and FilterOutput has `out0` (and `out1` with two outputs), each a vec4<f32>. It may call
	// `load0(texel: vec2<i32>)` .. `load3` (texels clamped to the input's size; an input not given reads 0) and read
	// `params.values[0..15]` (vec4<f32>). Everything outside the rectangles is left as it was.
	struct TextureFilterPass
	{
		std::string label{};
		std::string fragment{};
		std::vector<const Texture*> inputs{};
		// One or two, all the same size.
		std::vector<Texture*> outputs{};
		std::vector<TextureFilterQuad> quads{};
		// Up to parameterCount; the rest are zero.
		std::vector<glm::vec4> params{};
	};

	class TextureFilter
	{
	public:
		static constexpr std::size_t inputCount{ 4 };
		static constexpr std::size_t outputCount{ 2 };
		static constexpr std::size_t parameterCount{ 16 };

		// Records and submits the pass. Window thread only. Returns false (and draws nothing) for an invalid pass or a
		// source the GPU rejects.
		static bool run(const wgpu::Device& device, const wgpu::Queue& queue, const TextureFilterPass& pass);
		// Drops the cached pipelines.
		static void shutdown();
	};
}
