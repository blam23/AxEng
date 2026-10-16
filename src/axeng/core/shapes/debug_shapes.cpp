#include "axeng/core/shapes/debug_shapes.h"

#include <mutex>

namespace
{
	ax::ShapeList s_shapes;
	std::mutex s_shapes_mutex;
}

void ax::DebugShapes::add(Shape shape)
{
	std::lock_guard lock{ s_shapes_mutex };
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
	auto shapes{ take_queued() };
	if (shapes.empty())
		return;

	ShapeRenderer::draw(pass, shapes);
}

void ax::DebugShapes::clear()
{
	std::lock_guard lock{ s_shapes_mutex };
	s_shapes.clear();
}

ax::ShapeList ax::DebugShapes::take_queued()
{
	std::lock_guard lock{ s_shapes_mutex };
	auto shapes{ std::move(s_shapes) };
	s_shapes.clear();
	return shapes;
}

ax::ShapeList ax::DebugShapes::queued()
{
	std::lock_guard lock{ s_shapes_mutex };
	return s_shapes;
}
