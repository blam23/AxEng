#pragma once

#include "axeng/core/texture.h"

namespace ax
{
	class SpriteCuller
	{
	public:
		SpriteCuller(glm::vec2 viewport, glm::vec2 cameraPosition, float zoom);
		bool visible(const SpriteData& data, std::uint32_t textureWidth, std::uint32_t textureHeight) const;
	private:
		glm::dvec4 m_world{}, m_screen{};
		double m_worldPadding{ 1.0 };
		bool m_worldValid{}, m_screenValid{};
	};
}
