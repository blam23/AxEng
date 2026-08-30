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
		void handle_render_pass(wgpu::RenderPassEncoder& pass);
		void render_gui(wgpu::RenderPassEncoder& pass);
		void run_wgpu_render_pass();
		void init_pipeline();

		GLFWwindow* m_window{ nullptr };

		wgpu::Device m_device;
		wgpu::Queue m_queue;
		wgpu::Surface m_surface;
		wgpu::TextureFormat m_surfaceFormat{};
		wgpu::Color m_clearColor{ 0.0, 0.0, 0.0, 1.0 };
		wgpu::Texture m_depthTexture;
		wgpu::TextureView m_depthTextureView;
		wgpu::TextureFormat m_depthTextureFormat{ wgpu::TextureFormat::Depth24Plus };
		wgpu::ShaderModule m_shader;
		wgpu::RenderPipeline m_pipeline;
		wgpu::Buffer m_uniforms;
		wgpu::BindGroup m_uniformGroup;

		uint32_t m_width;
		uint32_t m_height;
		bool m_vsync;

		const char* s_shader_source =
		R"(
			@vertex
			fn vs_main(@builtin(vertex_index) in_vertex_index: u32) -> @builtin(position) vec4f {
				var p = vec2f(0.0, 0.0);
				if (in_vertex_index == 0u) {
					p = vec2f(-0.5, -0.5);
				} else if (in_vertex_index == 1u) {
					p = vec2f(0.5, -0.5);
				} else {
					p = vec2f(0.0, 0.5);
				}
				return vec4f(p, 0.0, 1.0);
			}
 
			@fragment
			fn fs_main() -> @location(0) vec4f {
				return vec4f(0.0, 0.4, 1.0, 1.0);
			}
		)";
	};
}