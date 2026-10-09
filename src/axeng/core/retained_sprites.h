#pragma once

#include "axeng/core/native_tasks.h"

namespace ax
{
	wgpu::Buffer upload_static_sprites(const wgpu::Device& device, const SpriteBuffer& data, glm::vec2 atlasOffset);
	class SpriteGroup
	{
	public:
		void set_visible(bool visible);
		bool visible() const { return !m_released && m_visible; }
		bool released() const { return m_released; }
		std::size_t size() const { return m_sprites.size(); }
		void check_thread() const;
	private:
		bool m_visible{ false };
		bool m_released{ false };
		Window* m_owner{};
		std::thread::id m_thread{ std::this_thread::get_id() };
		std::vector<SpriteDefinition*> m_sprites;
		friend class Window;
	};

	class StaticSpriteBatch
	{
	public:
		void set_visible(bool visible);
		bool visible() const { return !m_released && m_visible; }
		bool ready() const { return m_buffer != nullptr && !m_released; }
		bool released() const { return m_released; }
		std::size_t size() const { return m_data ? m_data->size() : 0; }
		const std::string& error() const { return m_error; }
		void release();
	private:
		void check_thread() const;
		bool m_visible{ false };
		bool m_released{ false };
		std::thread::id m_thread{ std::this_thread::get_id() };
		std::string m_error;
		std::shared_ptr<SpriteBuffer> m_data;
		Texture* m_texture{};
		glm::vec2 m_atlasOffset{};
		std::int64_t m_order{};
		wgpu::Buffer m_buffer;
		wgpu::BindGroup m_bindGroup;
		friend class Window;
	};
}
