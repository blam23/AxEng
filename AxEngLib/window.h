#pragma once

// Windows
#include <windows.h>

// STD
#include <string_view>
#include <deque>
#include <unordered_map>
#include <vector>

// GLFW
#include "glfw3webgpu.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// GFX
#include <webgpu/webgpu_cpp.h>
#include <webgpu/webgpu_cpp_print.h>

// GLM
#include "glm/glm.hpp"

// AxEng
#include "helpers.h"
#include "event.h"
#include "debug_view.h"

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

	struct WindowUpdateEvent
	{
		double delta;
	};

	struct WindowRequestCloseEvent
	{
		Window* window;
	};

	// Before the render pass is setup
	struct WindowPreRenderEvent
	{
		double delta;
	};

	// After the render pass is setup
	struct WindowRenderEvent
	{
		double delta;
		wgpu::RenderPassEncoder& pass;
	};

	struct WindowUIEvent
	{
		double delta;
	};

	using rectf = glm::vec4;

	class Window
	{
	public:
		DISABLE_COPY_AND_MOVE(Window);

		Window(const WindowDefinition&);
		~Window();

		bool init_webgpu();
		bool init_imgui();
		void run_loop();

		void prevent_close();

		GLFWwindow* glfw_handle() const
		{
			return m_window;
		}

		using UpdateEventHandler = EventHandler<WindowUpdateEvent>;
		UpdateEventHandler& get_update_event_handler()
		{
			return m_updateEventHandler;
		}

		using PreRenderEventHandler = EventHandler<WindowPreRenderEvent>;
		PreRenderEventHandler& get_pre_render_event_handler()
		{
			return m_preRenderEventHandler;
		}

		using RenderEventHandler = EventHandler<WindowRenderEvent>;
		RenderEventHandler& get_render_event_handler()
		{
			return m_renderEventHandler;
		}

		using UIEventHandler = EventHandler<WindowUIEvent>;
		UIEventHandler& get_ui_event_handler()
		{
			return m_uiEventHandler;
		}

		using RequestCloseEventHandler = EventHandler<WindowRequestCloseEvent>;
		RequestCloseEventHandler& get_request_close_event_handler()
		{
			return m_requestCloseEventHandler;
		}

		wgpu::Device& device() noexcept { return m_device; }
		const wgpu::Device& device() const noexcept { return m_device; }
		wgpu::Queue& queue() noexcept { return m_queue; }
		const wgpu::Queue& queue() const noexcept { return m_queue; }

		void set_clear_color(wgpu::Color& color)
		{
			m_clearColor = color;
		}

		wgpu::BindGroup setup_bind_groups(const wgpu::TextureView& view);
		void reload_pipeline();

		struct SpriteDefinition
		{
			Texture* tex{ nullptr };
			glm::vec2 pos{ 0.0f, 0.0f };
			rectf region{ 0.0f, 0.0f, 0.0f, 0.0f };
			bool useRegion{ false };
			float z{ 0.0f };
			glm::vec2 scale{ 1.0f, 1.0f };
		};

		SpriteDefinition* allocate_sprite();
		void free_sprite(SpriteDefinition* sprite);

		void render_texture(Texture* tex, glm::vec2 position);
		void render_texture(Texture* tex, glm::vec2 position, rectf region);

		// Optional overloads that accept a z-index for depth ordering
		void render_texture(Texture* tex, glm::vec2 position, float z);
		void render_texture(Texture* tex, glm::vec2 position, rectf region, float z);

	private:
		// Rendering
		void handle_render_pass(wgpu::RenderPassEncoder& pass, double delta);
		void render_gui(wgpu::RenderPassEncoder& pass, double delta);
		void run_wgpu_render_pass(double delta);
		void ensure_uniform_capacity(uint32_t required);

		// Logic
		void handle_tick(double delta);

		// Properties
		uint32_t m_width;
		uint32_t m_height;
		bool m_vsync;
		bool m_can_close{ true };

		// Events
		UpdateEventHandler m_updateEventHandler;
		PreRenderEventHandler m_preRenderEventHandler;
		RenderEventHandler m_renderEventHandler;
		UIEventHandler m_uiEventHandler;
		RequestCloseEventHandler m_requestCloseEventHandler;

		// GLFW
		GLFWwindow* m_window{ nullptr };
		void register_window_events();
		static void window_close_handler(GLFWwindow* window);

		// WGPU
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
		wgpu::Sampler m_nearestSampler;
		wgpu::BindGroupLayout m_groupLayout;
		wgpu::BindGroupLayout m_globalLayout;
		wgpu::Buffer m_viewportBuffer;
		wgpu::BindGroup m_viewportBindGroup;
		std::unordered_map<Texture*, wgpu::BindGroup> m_textureBindGroups;

		struct SpriteGroupRange
		{
			Texture* tex;
			uint32_t start;
			uint32_t count;
		};

		std::deque<SpriteDefinition> m_spriteSlots;
		std::vector<SpriteDefinition*> m_activeSprites;
		std::vector<SpriteDefinition*> m_freeSpriteSlots;
		std::vector<SpriteDefinition> m_pendingTextures;
		std::unordered_map<Texture*, std::vector<float>> m_spriteGroups;
		std::vector<SpriteGroupRange> m_spriteGroupRanges;
		std::vector<float> m_batchUniforms;
		size_t m_uniformStride{ 16 * sizeof(float) };
		uint32_t m_uniformsCapacity{ 1024 };
	};
}