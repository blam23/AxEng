#pragma once

#include "axeng/core/shapes/shape_renderer.h"

namespace ax
{
	// Queue of immediate-mode debug shapes. Anything added during a frame is drawn as a single
	// batch by flush() (called by the window at the end of the scene pass) and then cleared.
	class DebugShapes
	{
	public:
		// High enough to draw debug shapes over sprites and other shapes.
		static constexpr float default_z{ 100000.0f };
		static constexpr glm::vec4 default_colour{ 1.0f, 0.0f, 1.0f, 1.0f };

		static void add(Shape shape);

		static void rect_fill(glm::vec2 position, glm::vec2 size, const glm::vec4& colour);
		static void rect_outline(glm::vec2 position, glm::vec2 size, const glm::vec4& colour, float thickness);
		static void circle_fill(glm::vec2 centre, float radius, const glm::vec4& colour);
		static void circle_outline(glm::vec2 centre, float radius, const glm::vec4& colour, float thickness);
		static void line(glm::vec2 start, glm::vec2 end, const glm::vec4& colour, float thickness);

		// Draws and clears the queued shapes.
		static void flush(wgpu::RenderPassEncoder& pass);
		static void clear();
		static const ShapeList& queued();
	};
}
