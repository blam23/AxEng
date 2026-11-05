#include "axeng/core/sprite_culling.h"

#include <limits>

namespace
{
	bool finite(glm::vec2 value) { return std::isfinite(value.x) && std::isfinite(value.y); }
	bool contains(glm::dvec4 rect, glm::vec2 point)
	{
		return point.x >= rect.x && point.x <= rect.z && point.y >= rect.y && point.y <= rect.w;
	}
	double packed_pixel(float value)
	{
		return std::round(std::clamp(value, 0.0f, 65535.0f));
	}
	bool packable(float value)
	{
		// Packing uses lround before clamping; keep out-of-range conversions conservative.
		return std::isfinite(value) && std::abs(static_cast<double>(value)) < std::numeric_limits<long>::max();
	}
}

ax::SpriteCuller::SpriteCuller(glm::vec2 viewport, glm::vec2 cameraPosition, float zoom)
{
	m_screenValid = finite(viewport) && viewport.x > 0 && viewport.y > 0;
	if (!m_screenValid) return;
	m_screen = { 0, 0, viewport.x, viewport.y };
	m_worldValid = finite(cameraPosition) && std::isfinite(zoom) && zoom > 0;
	if (!m_worldValid) return;
	const glm::dvec2 half{ glm::dvec2{ viewport } / (2.0 * zoom) };
	m_world = { cameraPosition.x - half.x, cameraPosition.y - half.y,
		cameraPosition.x + half.x, cameraPosition.y + half.y };
	m_worldPadding = 1.0 / zoom + 16.0 * std::numeric_limits<float>::epsilon()
		* (std::abs(cameraPosition.x) + std::abs(cameraPosition.y) + half.x + half.y);
}

bool ax::SpriteCuller::visible(const SpriteData& data, std::uint32_t textureWidth, std::uint32_t textureHeight) const
{
	const bool screen{ data.screenSpace != 0 };
	if (!(screen ? m_screenValid : m_worldValid)) return true;
	const auto rect{ screen ? m_screen : m_world };
	const auto& clip{ data.clipRect };
	if (clip.z >= clip.x && clip.w >= clip.y)
	{
		if (!packable(clip.x) || !packable(clip.y) || !packable(clip.z) || !packable(clip.w)) return true;
		const auto left{ packed_pixel(clip.x) }, top{ packed_pixel(clip.y) };
		const auto right{ packed_pixel(clip.z) }, bottom{ packed_pixel(clip.w) };
		if (right <= left || bottom <= top || left >= m_screen.z || top >= m_screen.w || right <= 0 || bottom <= 0)
			return false;
	}
	// A false positive here is harmless and avoids region/size work for the common in-view case.
	if (contains(rect, data.pos)) return true;
	if (!finite(data.pos) || !finite(data.scale) || !std::isfinite(data.rotation) || textureWidth == 0 || textureHeight == 0)
		return true;
	double width{ static_cast<double>(textureWidth) }, height{ static_cast<double>(textureHeight) };
	if ((data.useRegion & sprite_flag_region) != 0)
	{
		if (!packable(data.region.x) || !packable(data.region.y) || !packable(data.region.z) || !packable(data.region.w)) return true;
		const auto regionWidth{ packed_pixel(data.region.z) }, regionHeight{ packed_pixel(data.region.w) };
		// WGSL selects the region when either packed dimension is nonzero.
		if (regionWidth != 0 || regionHeight != 0) { width = regionWidth; height = regionHeight; }
	}
	const float sizeX{ static_cast<float>(width) * data.scale.x };
	const float sizeY{ static_cast<float>(height) * data.scale.y };
	if (!std::isfinite(sizeX) || !std::isfinite(sizeY)) return true;
	const double centerX{ data.pos.x + static_cast<double>(sizeX) * 0.5 };
	const double centerY{ data.pos.y + static_cast<double>(sizeY) * 0.5 };
	// L1 radius encloses every rotation without depending on CPU/GPU angle reduction matching.
	const double radius{ 0.501 * (std::abs(static_cast<double>(sizeX)) + std::abs(static_cast<double>(sizeY))) };
	const double padding{ (screen ? 1.0 : m_worldPadding) + 16.0 * std::numeric_limits<float>::epsilon()
		* (std::abs(static_cast<double>(data.pos.x)) + std::abs(static_cast<double>(data.pos.y))
			+ std::abs(static_cast<double>(sizeX)) + std::abs(static_cast<double>(sizeY))) };
	return centerX + radius + padding >= rect.x && centerX - radius - padding <= rect.z
		&& centerY + radius + padding >= rect.y && centerY - radius - padding <= rect.w;
}
