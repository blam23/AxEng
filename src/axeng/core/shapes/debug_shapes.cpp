#include "axeng/core/shapes/debug_shapes.h"

namespace
{
	ax::ShapeList s_shapes;
}

void ax::DebugShapes::add(Shape shape)
{
	s_shapes.push_back(std::move(shape));
}

void ax::DebugShapes::rect_fill(glm::vec2 position, glm::vec2 size, const glm::vec4& colour)
{
	add(ax::Rectangle{ .position = position, .size = size, .fill = colour, .z = default_z });
}

void ax::DebugShapes::rect_outline(glm::vec2 position, glm::vec2 size, const glm::vec4& colour, float thickness)
{
	add(ax::Rectangle{ .position = position, .size = size, .fill = glm::vec4{ 0.0f },
		.outline = { .colour = colour, .thickness = thickness }, .z = default_z });
}

void ax::DebugShapes::circle_fill(glm::vec2 centre, float radius, const glm::vec4& colour)
{
	add(ax::Circle{ .position = centre, .radius = radius, .fill = colour, .z = default_z });
}

void ax::DebugShapes::circle_outline(glm::vec2 centre, float radius, const glm::vec4& colour, float thickness)
{
	add(ax::Circle{ .position = centre, .radius = radius, .fill = glm::vec4{ 0.0f },
		.outline = { .colour = colour, .thickness = thickness }, .z = default_z });
}

void ax::DebugShapes::line(glm::vec2 start, glm::vec2 end, const glm::vec4& colour, float thickness)
{
	add(ax::Line{ .start = start, .end = end, .thickness = thickness, .fill = colour, .z = default_z });
}

void ax::DebugShapes::flush(wgpu::RenderPassEncoder& pass)
{
	if (s_shapes.empty())
		return;

	ShapeRenderer::draw(pass, s_shapes);
	s_shapes.clear();
}

void ax::DebugShapes::clear()
{
	s_shapes.clear();
}

const ax::ShapeList& ax::DebugShapes::queued()
{
	return s_shapes;
}
