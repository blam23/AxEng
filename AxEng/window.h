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

	struct WindowDefinition
	{
		uint32_t width{ 1920 };
		uint32_t height{ 1080 };
		std::string title{ "AxEng" };
		bool vsync{ true };
	};

	class Window
	{
	public:
		DISABLE_COPY_AND_MOVE(Window);

		Window(const WindowDefinition&);
		~Window();

		GLFWwindow* glfw_handle() const
		{
			return m_window;
		}

		bool init_wegbpu();
		bool init_imgui();
		void run_loop();

	private:
		GLFWwindow* m_window{ nullptr };

		wgpu::Device m_device;
		wgpu::Queue m_queue;
		wgpu::Surface m_surface;
		wgpu::TextureFormat m_surfaceFormat{};
		wgpu::Color m_clearColor{ 0.0, 0.0, 0.0, 1.0 };
		wgpu::Texture m_depthTexture;
		wgpu::TextureView m_depthTextureView;
		wgpu::TextureFormat m_depthTextureFormat{ wgpu::TextureFormat::Depth24Plus };

		uint32_t m_width;
		uint32_t m_height;
		bool m_vsync;
	};
}