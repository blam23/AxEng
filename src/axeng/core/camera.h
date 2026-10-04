#pragma once

#include "glm/glm.hpp"

namespace ax
{
	class Camera
	{
	public:
		const glm::vec2& position() const noexcept { return m_position; }
		void set_position(glm::vec2 position) noexcept { m_position = position; }
		void translate(glm::vec2 delta) noexcept { m_position += delta; }

		float zoom() const noexcept { return m_zoom; }
		void set_zoom(float zoom) noexcept
		{
			if (zoom > 0.0f)
				m_zoom = zoom;
		}

	private:
		glm::vec2 m_position{ 0.0f, 0.0f };
		float m_zoom{ 1.0f };
	};
}
