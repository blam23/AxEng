#pragma once

// STD
#include <string_view>

// GLFW
#include "glfw3webgpu.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// GFX
#include <webgpu/webgpu_cpp.h>
#include <webgpu/webgpu_cpp_print.h>

// AxEng
#include "helpers.h"

namespace ax
{
	bool setup_glfw();
	void teardown_glfw();

	class Window
	{
	public:
		DISABLE_COPY_AND_MOVE(Window);

		Window(uint32_t width, uint32_t height, const std::string_view title);
		~Window();

		GLFWwindow* glfw_handle() const
		{
			return m_window;
		}

		bool init_wegbpu();
		void run_loop();

	private:
		GLFWwindow* m_window{ nullptr };

		wgpu::Device m_device;
		wgpu::Queue m_queue;
		wgpu::Surface m_surface;

		uint32_t m_width;
		uint32_t m_height;
	};
}