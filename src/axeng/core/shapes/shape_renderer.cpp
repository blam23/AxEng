#include "axeng/core/shapes/shape_renderer.h"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <numbers>
#include <numeric>
#include <utility>

namespace
{
	// #embed paths are resolved relative to this file. The data is not null terminated.
	constexpr unsigned char s_shape_shader[] = {
#embed "shapes.wgsl"
	};

	struct ShapeVertex
	{
		glm::vec2 pos;
		float z;
		std::uint32_t screenSpace;
		std::uint32_t colour; // RGBA8, unorm8x4
	};
	static_assert(sizeof(ShapeVertex) == 20);

	struct ElementRange
	{
		std::uint32_t firstVertex;
		std::uint32_t vertexCount;
		std::uint32_t firstIndex;
		std::uint32_t indexCount;
	};

	struct Mesh
	{
		std::vector<ShapeVertex> vertices;
		std::vector<std::uint32_t> indices;
		std::vector<ElementRange> elements;

		void clear()
		{
			vertices.clear();
			indices.clear();
			elements.clear();
		}
	};

	using Loop = std::vector<glm::vec2>;

	constexpr std::uint64_t s_initial_arena_size{ 64 * 1024 };
	constexpr std::uint64_t s_max_chunk_size{ 16 * 1024 * 1024 };
	constexpr std::uint32_t s_min_circle_segments{ 12 };
	constexpr std::uint32_t s_max_circle_segments{ 256 };
	constexpr float s_tessellation_tolerance{ 0.2f }; // max deviation from the true curve, in pixels
	constexpr float s_pi{ std::numbers::pi_v<float> };

	struct State
	{
		wgpu::Device device;
		wgpu::Queue queue;
		ax::ShapeRendererConfig config;
		wgpu::RenderPipeline opaquePipeline;
		wgpu::RenderPipeline translucentPipeline;
		wgpu::Buffer viewBuffer;
		wgpu::BindGroup viewBindGroup;

		wgpu::Buffer arena;
		std::uint64_t arenaCapacity{ 0 };
		std::uint64_t arenaOffset{ 0 };
		std::uint64_t maxChunkSize{ s_max_chunk_size };

		ax::ShapeView view{};
		bool hasView{ false };

		// Reused between calls to avoid reallocating every frame.
		Mesh opaque;
		Mesh translucent;
		std::vector<std::size_t> order;
		std::vector<std::uint32_t> rebased;
		Loop outer;
		Loop inner;
		std::vector<std::uint32_t> triangulation;

		ax::ShapeDrawStats stats{};
	};

	std::unique_ptr<State> s_state{};

	std::uint32_t pack_colour(const glm::vec4& colour)
	{
		const auto channel = [](float v) -> std::uint32_t
		{
			if (!(v > 0.0f))
				return 0;
			return static_cast<std::uint32_t>(std::min(v, 1.0f) * 255.0f + 0.5f);
		};
		return channel(colour.r) | (channel(colour.g) << 8) | (channel(colour.b) << 16) | (channel(colour.a) << 24);
	}

	std::uint32_t alpha_of(std::uint32_t packed) { return packed >> 24; }

	float cross(const glm::vec2& a, const glm::vec2& b) { return a.x * b.y - a.y * b.x; }

	float signed_area(const Loop& loop)
	{
		float area{ 0.0f };
		for (std::size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++)
			area += cross(loop[j], loop[i]);
		return area * 0.5f;
	}

	// Matches the sprite shader: positive rotation is clockwise on screen (y down).
	glm::vec2 rotate(const glm::vec2& v, float c, float s)
	{
		return { v.x * c - v.y * s, v.x * s + v.y * c };
	}

	void transform_loop(Loop& loop, const glm::vec2& pivot, float rotation)
	{
		const float c{ std::cos(rotation) };
		const float s{ std::sin(rotation) };
		for (auto& p : loop)
			p = pivot + rotate(p, c, s);
	}

	std::uint32_t full_circle_segments(float radiusPixels)
	{
		if (!(radiusPixels > s_tessellation_tolerance))
			return s_min_circle_segments;
		const float step{ 2.0f * std::acos(1.0f - s_tessellation_tolerance / radiusPixels) };
		const float segments{ std::ceil(2.0f * s_pi / step) };
		return std::clamp(static_cast<std::uint32_t>(std::min(segments, 4096.0f)), s_min_circle_segments, s_max_circle_segments);
	}

	std::uint32_t arc_segments(std::uint32_t requested, float radius, float pixelScale, float fraction)
	{
		if (requested > 0)
			return requested;
		const auto full{ full_circle_segments(radius * pixelScale) };
		return std::max(2u, static_cast<std::uint32_t>(std::ceil(static_cast<float>(full) * fraction)));
	}

	// Appends points on an arc, including both end points.
	void append_arc(Loop& loop, const glm::vec2& centre, glm::vec2 radii, float start, float sweep, std::uint32_t segments)
	{
		for (std::uint32_t i = 0; i <= segments; ++i)
		{
			const float a{ start + sweep * static_cast<float>(i) / static_cast<float>(segments) };
			loop.push_back(centre + glm::vec2{ std::cos(a), std::sin(a) } * radii);
		}
	}

	// Removes duplicate and collinear points and makes the winding positive (CCW in maths axes).
	bool normalise_loop(Loop& loop)
	{
		const auto nearly_equal = [](const glm::vec2& a, const glm::vec2& b)
		{
			const auto d{ a - b };
			return glm::dot(d, d) <= 1e-10f * std::max(1.0f, glm::dot(a, a));
		};

		bool changed{ true };
		while (changed && loop.size() >= 3)
		{
			changed = false;
			for (std::size_t i = 0; i < loop.size() && loop.size() >= 3;)
			{
				const auto& prev{ loop[(i + loop.size() - 1) % loop.size()] };
				const auto& cur{ loop[i] };
				const auto& next{ loop[(i + 1) % loop.size()] };
				const auto e0{ cur - prev };
				const auto e1{ next - cur };
				const float scale{ std::sqrt(glm::dot(e0, e0) * glm::dot(e1, e1)) };
				if (nearly_equal(prev, cur) || (std::abs(cross(e0, e1)) <= 1e-6f * scale && glm::dot(e0, e1) > 0.0f))
				{
					loop.erase(loop.begin() + static_cast<std::ptrdiff_t>(i));
					changed = true;
				}
				else
					++i;
			}
		}

		if (loop.size() < 3)
			return false;

		const float area{ signed_area(loop) };
		if (!std::isfinite(area) || std::abs(area) < 1e-6f)
			return false;
		if (area < 0.0f)
			std::reverse(loop.begin(), loop.end());
		return true;
	}

	bool is_convex(const Loop& loop)
	{
		for (std::size_t i = 0; i < loop.size(); ++i)
		{
			const auto& a{ loop[(i + loop.size() - 1) % loop.size()] };
			const auto& b{ loop[i] };
			const auto& c{ loop[(i + 1) % loop.size()] };
			if (cross(b - a, c - b) < 0.0f)
				return false;
		}
		return true;
	}

	// Offsets every edge inwards by thickness. Returns false if the outline swallows the shape.
	bool inset_loop(const Loop& loop, float thickness, Loop& out)
	{
		const auto count{ loop.size() };
		out.resize(count);
		for (std::size_t i = 0; i < count; ++i)
		{
			const auto& prev{ loop[(i + count - 1) % count] };
			const auto& cur{ loop[i] };
			const auto& next{ loop[(i + 1) % count] };
			const auto d0{ glm::normalize(cur - prev) };
			const auto d1{ glm::normalize(next - cur) };
			const glm::vec2 n0{ -d0.y, d0.x };
			const glm::vec2 n1{ -d1.y, d1.x };
			auto miter{ n0 + n1 };
			const float len{ glm::length(miter) };
			miter = len > 1e-4f ? miter / len : n0;
			const float denom{ std::max(glm::dot(miter, n0), 1e-4f) };
			out[i] = cur + miter * (thickness / denom);
		}

		for (std::size_t i = 0; i < count; ++i)
		{
			const auto j{ (i + 1) % count };
			if (glm::dot(out[j] - out[i], loop[j] - loop[i]) <= 0.0f)
				return false;
		}
		return signed_area(out) > 0.0f;
	}

	bool point_in_triangle(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c)
	{
		return cross(b - a, p - a) >= 0.0f && cross(c - b, p - b) >= 0.0f && cross(a - c, p - c) >= 0.0f;
	}

	// Triangulates a positively wound simple polygon into local indices.
	void triangulate(const Loop& loop, bool convex, std::vector<std::uint32_t>& out)
	{
		out.clear();
		const auto count{ static_cast<std::uint32_t>(loop.size()) };
		if (convex)
		{
			for (std::uint32_t i = 1; i + 1 < count; ++i)
				out.insert(out.end(), { 0u, i, i + 1 });
			return;
		}

		// Ear clipping
		std::vector<std::uint32_t> remaining(count);
		std::iota(remaining.begin(), remaining.end(), 0u);
		while (remaining.size() > 3)
		{
			const auto n{ remaining.size() };
			std::size_t ear{ n };
			std::size_t fallback{ 0 };
			float fallbackCross{ -std::numeric_limits<float>::infinity() };
			for (std::size_t i = 0; i < n; ++i)
			{
				const auto ia{ remaining[(i + n - 1) % n] };
				const auto ib{ remaining[i] };
				const auto ic{ remaining[(i + 1) % n] };
				const auto& a{ loop[ia] };
				const auto& b{ loop[ib] };
				const auto& c{ loop[ic] };
				const float turn{ cross(b - a, c - b) };
				if (turn > fallbackCross)
				{
					fallbackCross = turn;
					fallback = i;
				}
				if (turn <= 0.0f)
					continue;

				bool contains{ false };
				for (const auto other : remaining)
				{
					if (other == ia || other == ib || other == ic)
						continue;
					const auto& p{ loop[other] };
					if (p == a || p == b || p == c)
						continue;
					if (point_in_triangle(p, a, b, c))
					{
						contains = true;
						break;
					}
				}
				if (!contains)
				{
					ear = i;
					break;
				}
			}

			// Only reachable for degenerate input; clipping the most convex vertex keeps progress.
			if (ear == n)
				ear = fallback;

			out.insert(out.end(), { remaining[(ear + n - 1) % n], remaining[ear], remaining[(ear + 1) % n] });
			remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(ear));
		}
		out.insert(out.end(), remaining.begin(), remaining.end());
	}

	struct ShapeParams
	{
		glm::vec4 fill;
		ax::ShapeOutline outline;
		float z;
		bool screenSpace;
	};

	// Builds the boundary of a shape in final (rotated, positioned) pixel coordinates.
	struct LoopBuilder
	{
		float pixelScale;
		Loop& loop;

		void operator()(const ax::Circle& s) const
		{
			const auto segments{ s.segments > 0 ? std::max(3u, s.segments) : full_circle_segments(s.radius * pixelScale) };
			append_arc(loop, {}, glm::vec2{ s.radius }, 0.0f, 2.0f * s_pi, segments);
			loop.pop_back();
			transform_loop(loop, s.position, s.rotation);
		}

		void operator()(const ax::Ellipse& s) const
		{
			const float radius{ std::max(std::abs(s.radii.x), std::abs(s.radii.y)) };
			const auto segments{ s.segments > 0 ? std::max(3u, s.segments) : full_circle_segments(radius * pixelScale) };
			append_arc(loop, {}, s.radii, 0.0f, 2.0f * s_pi, segments);
			loop.pop_back();
			transform_loop(loop, s.position, s.rotation);
		}

		void operator()(const ax::Rectangle& s) const
		{
			const glm::vec2 half{ glm::abs(s.size) * 0.5f };
			const float r{ std::clamp(s.cornerRadius, 0.0f, std::min(half.x, half.y)) };
			if (r <= 0.0f)
			{
				loop.insert(loop.end(), { { half.x, half.y }, { -half.x, half.y }, { -half.x, -half.y }, { half.x, -half.y } });
			}
			else
			{
				const auto segments{ arc_segments(s.segments, r, pixelScale, 0.25f) };
				const glm::vec2 inner{ half - glm::vec2{ r } };
				append_arc(loop, { inner.x, inner.y }, glm::vec2{ r }, 0.0f, s_pi * 0.5f, segments);
				append_arc(loop, { -inner.x, inner.y }, glm::vec2{ r }, s_pi * 0.5f, s_pi * 0.5f, segments);
				append_arc(loop, { -inner.x, -inner.y }, glm::vec2{ r }, s_pi, s_pi * 0.5f, segments);
				append_arc(loop, { inner.x, -inner.y }, glm::vec2{ r }, s_pi * 1.5f, s_pi * 0.5f, segments);
			}
			transform_loop(loop, s.position + s.size * 0.5f, s.rotation);
		}

		void operator()(const ax::Triangle& s) const
		{
			for (const auto& p : s.points)
				loop.push_back(p * s.scale);
			transform_loop(loop, s.position, s.rotation);
		}

		void operator()(const ax::Polygon& s) const
		{
			for (const auto& p : s.points)
				loop.push_back(p * s.scale);
			transform_loop(loop, s.position, s.rotation);
		}

		void operator()(const ax::Line& s) const
		{
			const float h{ std::abs(s.thickness) * 0.5f };
			if (h <= 0.0f)
				return;

			const glm::vec2 mid{ (s.start + s.end) * 0.5f };
			const glm::vec2 a{ s.start - mid };
			const glm::vec2 b{ s.end - mid };
			const float length{ glm::length(b - a) };
			const glm::vec2 d{ length > 1e-6f ? (b - a) / length : glm::vec2{ 1.0f, 0.0f } };
			const glm::vec2 n{ -d.y, d.x };

			switch (s.cap)
			{
			case ax::LineCap::Butt:
				loop.insert(loop.end(), { a + n * h, a - n * h, b - n * h, b + n * h });
				break;
			case ax::LineCap::Square:
				loop.insert(loop.end(), { a - d * h + n * h, a - d * h - n * h, b + d * h - n * h, b + d * h + n * h });
				break;
			case ax::LineCap::Round:
			{
				const auto segments{ arc_segments(s.segments, h, pixelScale, 0.5f) };
				const float base{ std::atan2(n.y, n.x) };
				// Cap around b sweeps from -n through +d to +n, cap around a from +n through -d to -n.
				append_arc(loop, b, glm::vec2{ h }, base + s_pi, s_pi, segments);
				append_arc(loop, a, glm::vec2{ h }, base, s_pi, segments);
				break;
			}
			}
			transform_loop(loop, mid, s.rotation);
		}
	};

	ShapeParams params_of(const ax::Shape& shape)
	{
		return std::visit([](const auto& s) { return ShapeParams{ s.fill, s.outline, s.z, s.screenSpace }; }, shape);
	}

	void begin_element(Mesh& mesh)
	{
		mesh.elements.push_back({
			static_cast<std::uint32_t>(mesh.vertices.size()), 0,
			static_cast<std::uint32_t>(mesh.indices.size()), 0 });
	}

	void end_element(Mesh& mesh)
	{
		auto& e{ mesh.elements.back() };
		e.vertexCount = static_cast<std::uint32_t>(mesh.vertices.size()) - e.firstVertex;
		e.indexCount = static_cast<std::uint32_t>(mesh.indices.size()) - e.firstIndex;
	}

	void append_vertices(Mesh& mesh, const Loop& loop, const ShapeParams& p, std::uint32_t colour)
	{
		for (const auto& pos : loop)
			mesh.vertices.push_back({ pos, p.z, p.screenSpace ? 1u : 0u, colour });
	}

	void emit_fill(Mesh& mesh, const Loop& loop, const std::vector<std::uint32_t>& triangles, const ShapeParams& p, std::uint32_t colour)
	{
		begin_element(mesh);
		const auto base{ static_cast<std::uint32_t>(mesh.vertices.size()) };
		append_vertices(mesh, loop, p, colour);
		for (const auto idx : triangles)
			mesh.indices.push_back(base + idx);
		end_element(mesh);
	}

	void emit_ring(Mesh& mesh, const Loop& outer, const Loop& inner, const ShapeParams& p, std::uint32_t colour)
	{
		begin_element(mesh);
		const auto base{ static_cast<std::uint32_t>(mesh.vertices.size()) };
		const auto count{ static_cast<std::uint32_t>(outer.size()) };
		append_vertices(mesh, outer, p, colour);
		append_vertices(mesh, inner, p, colour);
		for (std::uint32_t i = 0; i < count; ++i)
		{
			const auto j{ (i + 1) % count };
			const auto o0{ base + i }, o1{ base + j }, i0{ base + count + i }, i1{ base + count + j };
			mesh.indices.insert(mesh.indices.end(), { o0, o1, i1, o0, i1, i0 });
		}
		end_element(mesh);
	}

	Mesh& mesh_for(State& state, std::uint32_t colour)
	{
		return alpha_of(colour) == 255 ? state.opaque : state.translucent;
	}

	void tessellate(State& state, const ax::Shape& shape)
	{
		const auto params{ params_of(shape) };
		const float pixelScale{ params.screenSpace ? 1.0f : std::max(state.view.zoom, 1e-3f) };

		state.outer.clear();
		std::visit(LoopBuilder{ pixelScale, state.outer }, shape);
		if (!normalise_loop(state.outer))
			return;

		const auto fillColour{ pack_colour(params.fill) };
		const auto outlineColour{ pack_colour(params.outline.colour) };
		const bool hasOutline{ params.outline.thickness > 0.0f && alpha_of(outlineColour) > 0 };
		const bool convex{ is_convex(state.outer) };

		if (!hasOutline)
		{
			if (alpha_of(fillColour) == 0)
				return;
			triangulate(state.outer, convex, state.triangulation);
			emit_fill(mesh_for(state, fillColour), state.outer, state.triangulation, params, fillColour);
			return;
		}

		if (!inset_loop(state.outer, params.outline.thickness, state.inner))
		{
			triangulate(state.outer, convex, state.triangulation);
			emit_fill(mesh_for(state, outlineColour), state.outer, state.triangulation, params, outlineColour);
			return;
		}

		if (alpha_of(fillColour) > 0)
		{
			triangulate(state.inner, convex, state.triangulation);
			emit_fill(mesh_for(state, fillColour), state.inner, state.triangulation, params, fillColour);
		}
		emit_ring(mesh_for(state, outlineColour), state.outer, state.inner, params, outlineColour);
	}

	// Returns the offset in the arena that has room for size bytes.
	std::uint64_t allocate(State& state, std::uint64_t size)
	{
		if (state.arena == nullptr || state.arenaOffset + size > state.arenaCapacity)
		{
			// The previous buffer stays alive for as long as already-recorded commands reference it.
			auto capacity{ std::max(state.arenaCapacity * 2, s_initial_arena_size) };
			while (capacity < size)
				capacity *= 2;
			capacity = std::max(std::min(capacity, state.maxChunkSize), size);

			wgpu::BufferDescriptor desc{};
			desc.label = "ShapeArena";
			desc.size = capacity;
			desc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Vertex | wgpu::BufferUsage::Index;
			state.arena = state.device.CreateBuffer(&desc);
			state.arenaCapacity = capacity;
			state.arenaOffset = 0;
		}

		const auto offset{ state.arenaOffset };
		state.arenaOffset += size;
		return offset;
	}

	void draw_chunk(State& state, wgpu::RenderPassEncoder& pass, const Mesh& mesh, std::size_t firstElement, std::size_t endElement)
	{
		const auto& first{ mesh.elements[firstElement] };
		const auto& last{ mesh.elements[endElement - 1] };
		const auto vertexCount{ last.firstVertex + last.vertexCount - first.firstVertex };
		const auto indexCount{ last.firstIndex + last.indexCount - first.firstIndex };
		const std::uint64_t vertexBytes{ vertexCount * sizeof(ShapeVertex) };
		const std::uint64_t indexBytes{ indexCount * sizeof(std::uint32_t) };

		const auto offset{ allocate(state, vertexBytes + indexBytes) };
		state.queue.WriteBuffer(state.arena, offset, mesh.vertices.data() + first.firstVertex, vertexBytes);

		const std::uint32_t* indices{ mesh.indices.data() + first.firstIndex };
		if (first.firstVertex != 0)
		{
			state.rebased.assign(indices, indices + indexCount);
			for (auto& i : state.rebased)
				i -= first.firstVertex;
			indices = state.rebased.data();
		}
		state.queue.WriteBuffer(state.arena, offset + vertexBytes, indices, indexBytes);

		pass.SetVertexBuffer(0, state.arena, offset, vertexBytes);
		pass.SetIndexBuffer(state.arena, wgpu::IndexFormat::Uint32, offset + vertexBytes, indexBytes);
		pass.DrawIndexed(indexCount);

		state.stats.drawCalls++;
		state.stats.vertices += vertexCount;
		state.stats.indices += indexCount;
	}

	void draw_mesh(State& state, wgpu::RenderPassEncoder& pass, const Mesh& mesh, const wgpu::RenderPipeline& pipeline)
	{
		if (mesh.elements.empty())
			return;

		pass.SetPipeline(pipeline);
		pass.SetBindGroup(0, state.viewBindGroup);

		// Split at element boundaries so a single upload never exceeds the device buffer limit.
		std::size_t chunkStart{ 0 };
		std::uint64_t chunkBytes{ 0 };
		for (std::size_t i = 0; i < mesh.elements.size(); ++i)
		{
			const auto& e{ mesh.elements[i] };
			const std::uint64_t bytes{ e.vertexCount * sizeof(ShapeVertex) + e.indexCount * sizeof(std::uint32_t) };
			if (bytes > state.maxChunkSize)
			{
				spdlog::error("ShapeRenderer: shape needs {} bytes which exceeds the {} byte buffer limit, skipping", bytes, state.maxChunkSize);
				if (i > chunkStart)
					draw_chunk(state, pass, mesh, chunkStart, i);
				chunkStart = i + 1;
				chunkBytes = 0;
				continue;
			}
			if (chunkBytes + bytes > state.maxChunkSize)
			{
				draw_chunk(state, pass, mesh, chunkStart, i);
				chunkStart = i;
				chunkBytes = 0;
			}
			chunkBytes += bytes;
		}
		if (chunkStart < mesh.elements.size())
			draw_chunk(state, pass, mesh, chunkStart, mesh.elements.size());
	}

	wgpu::RenderPipeline create_pipeline(State& state, const wgpu::ShaderModule& shader, const wgpu::PipelineLayout& layout, bool translucent)
	{
		wgpu::VertexAttribute attributes[4]{};
		attributes[0] = { .format = wgpu::VertexFormat::Float32x2, .offset = offsetof(ShapeVertex, pos), .shaderLocation = 0 };
		attributes[1] = { .format = wgpu::VertexFormat::Float32, .offset = offsetof(ShapeVertex, z), .shaderLocation = 1 };
		attributes[2] = { .format = wgpu::VertexFormat::Uint32, .offset = offsetof(ShapeVertex, screenSpace), .shaderLocation = 2 };
		attributes[3] = { .format = wgpu::VertexFormat::Unorm8x4, .offset = offsetof(ShapeVertex, colour), .shaderLocation = 3 };

		wgpu::VertexBufferLayout vertexLayout{};
		vertexLayout.stepMode = wgpu::VertexStepMode::Vertex;
		vertexLayout.arrayStride = sizeof(ShapeVertex);
		vertexLayout.attributeCount = 4;
		vertexLayout.attributes = attributes;

		wgpu::BlendState blend{};
		blend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
		blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
		blend.color.operation = wgpu::BlendOperation::Add;
		blend.alpha.srcFactor = wgpu::BlendFactor::One;
		blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
		blend.alpha.operation = wgpu::BlendOperation::Add;

		wgpu::ColorTargetState colourTarget{};
		colourTarget.format = state.config.colourFormat;
		colourTarget.blend = translucent ? &blend : nullptr;
		colourTarget.writeMask = wgpu::ColorWriteMask::All;

		wgpu::FragmentState fragment{};
		fragment.module = shader;
		fragment.entryPoint = "fs_main";
		fragment.targetCount = 1;
		fragment.targets = &colourTarget;

		// Reverse depth like the sprite pipeline; GreaterEqual lets later shapes win ties.
		wgpu::DepthStencilState depth{};
		depth.format = state.config.depthFormat;
		depth.depthWriteEnabled = translucent ? wgpu::OptionalBool::False : wgpu::OptionalBool::True;
		depth.depthCompare = wgpu::CompareFunction::GreaterEqual;

		wgpu::RenderPipelineDescriptor desc{};
		desc.label = translucent ? "ShapeTranslucent" : "ShapeOpaque";
		desc.layout = layout;
		desc.vertex.module = shader;
		desc.vertex.entryPoint = "vs_main";
		desc.vertex.bufferCount = 1;
		desc.vertex.buffers = &vertexLayout;
		desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
		desc.primitive.cullMode = wgpu::CullMode::None;
		desc.depthStencil = state.config.depthFormat != wgpu::TextureFormat::Undefined ? &depth : nullptr;
		desc.multisample.count = state.config.sampleCount;
		desc.multisample.mask = ~0u;
		desc.fragment = &fragment;
		return state.device.CreateRenderPipeline(&desc);
	}
}

bool ax::ShapeRenderer::init(const wgpu::Device& device, const ShapeRendererConfig& config)
{
	if (device == nullptr || config.colourFormat == wgpu::TextureFormat::Undefined)
	{
		spdlog::error("ShapeRenderer: init requires a device and a colour format");
		return false;
	}

	if (s_state && s_state->device.Get() == device.Get() && s_state->config == config)
		return true;

	auto state{ std::make_unique<State>() };
	state->device = device;
	state->queue = device.GetQueue();
	state->config = config;

	wgpu::Limits limits{};
	if (device.GetLimits(&limits) && limits.maxBufferSize != wgpu::kLimitU64Undefined)
		state->maxChunkSize = std::min(s_max_chunk_size, limits.maxBufferSize) & ~std::uint64_t{ 3 };

	wgpu::ShaderSourceWGSL source{};
	source.code = { reinterpret_cast<const char*>(s_shape_shader), sizeof(s_shape_shader) };
	wgpu::ShaderModuleDescriptor shaderDesc{};
	shaderDesc.nextInChain = &source;
	shaderDesc.label = "ShapeShader";
	const auto shader{ device.CreateShaderModule(&shaderDesc) };

	wgpu::BindGroupLayoutEntry viewEntry{};
	viewEntry.binding = 0;
	viewEntry.visibility = wgpu::ShaderStage::Vertex;
	viewEntry.buffer.type = wgpu::BufferBindingType::Uniform;
	viewEntry.buffer.minBindingSize = 8 * sizeof(float);
	wgpu::BindGroupLayoutDescriptor groupLayoutDesc{};
	groupLayoutDesc.entryCount = 1;
	groupLayoutDesc.entries = &viewEntry;
	const auto groupLayout{ device.CreateBindGroupLayout(&groupLayoutDesc) };

	wgpu::PipelineLayoutDescriptor layoutDesc{};
	layoutDesc.bindGroupLayoutCount = 1;
	layoutDesc.bindGroupLayouts = &groupLayout;
	const auto layout{ device.CreatePipelineLayout(&layoutDesc) };

	wgpu::BufferDescriptor viewDesc{};
	viewDesc.label = "ShapeView";
	viewDesc.size = 8 * sizeof(float);
	viewDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform;
	state->viewBuffer = device.CreateBuffer(&viewDesc);

	wgpu::BindGroupEntry viewBinding{};
	viewBinding.binding = 0;
	viewBinding.buffer = state->viewBuffer;
	viewBinding.size = viewDesc.size;
	wgpu::BindGroupDescriptor bindDesc{};
	bindDesc.layout = groupLayout;
	bindDesc.entryCount = 1;
	bindDesc.entries = &viewBinding;
	state->viewBindGroup = device.CreateBindGroup(&bindDesc);

	state->opaquePipeline = create_pipeline(*state, shader, layout, false);
	state->translucentPipeline = create_pipeline(*state, shader, layout, true);

	// Keep the current view so re-initialising (e.g. on resize) mid-frame keeps working.
	if (s_state)
	{
		state->view = s_state->view;
		state->hasView = s_state->hasView;
	}
	s_state = std::move(state);
	if (s_state->hasView)
		begin_frame(s_state->view);
	return true;
}

void ax::ShapeRenderer::shutdown()
{
	s_state.reset();
}

bool ax::ShapeRenderer::is_initialised()
{
	return s_state != nullptr;
}

void ax::ShapeRenderer::begin_frame(const ShapeView& view)
{
	if (!s_state)
		return;

	s_state->view = view;
	s_state->hasView = true;
	s_state->arenaOffset = 0;

	const float data[8]{
		view.viewport.x, view.viewport.y, 0.0f, 0.0f,
		view.cameraPosition.x, view.cameraPosition.y, view.zoom, 0.0f,
	};
	s_state->queue.WriteBuffer(s_state->viewBuffer, 0, data, sizeof(data));
}

void ax::ShapeRenderer::draw(wgpu::RenderPassEncoder& pass, const ShapeList& shapes)
{
	if (!s_state)
	{
		static bool warned{ false };
		if (!std::exchange(warned, true))
			spdlog::error("ShapeRenderer::draw called before ShapeRenderer::init");
		return;
	}

	auto& state{ *s_state };
	state.stats = {};
	if (shapes.empty())
		return;

	if (!state.hasView || state.view.viewport.x <= 0.0f || state.view.viewport.y <= 0.0f)
	{
		spdlog::error("ShapeRenderer::draw called without a valid view, call ShapeRenderer::begin_frame first");
		return;
	}

	// Back to front, so translucent shapes blend in z order; ties keep submission order.
	state.order.resize(shapes.size());
	std::iota(state.order.begin(), state.order.end(), std::size_t{ 0 });
	std::stable_sort(state.order.begin(), state.order.end(), [&shapes](std::size_t a, std::size_t b)
		{
			const auto za{ std::visit([](const auto& s) { return s.z; }, shapes[a]) };
			const auto zb{ std::visit([](const auto& s) { return s.z; }, shapes[b]) };
			return za < zb;
		});

	state.opaque.clear();
	state.translucent.clear();
	for (const auto i : state.order)
		tessellate(state, shapes[i]);

	// Opaque first so translucent geometry can blend over it regardless of submission order.
	draw_mesh(state, pass, state.opaque, state.opaquePipeline);
	draw_mesh(state, pass, state.translucent, state.translucentPipeline);
}

ax::ShapeDrawStats ax::ShapeRenderer::last_draw_stats()
{
	return s_state ? s_state->stats : ShapeDrawStats{};
}
