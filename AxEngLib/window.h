#pragma once

// Windows
#include <windows.h>

// STD
#include <string_view>
#include <deque>
#include <unordered_map>
#include <vector>
#include <memory>

// GLFW
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// GFX
#include "vulkan_context.h"

// AxEng
#include "helpers.h"
#include "event.h"
#include "texture.h"
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
		VkCommandBuffer commandBuffer;
	};

	struct WindowUIEvent
	{
		double delta;
	};


	class Window
	{
	public:
		DISABLE_COPY_AND_MOVE(Window);

		Window(const WindowDefinition&);
		~Window();

		bool init_vulkan();
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

		const std::shared_ptr<VulkanContext>& vulkan_context() const noexcept { return m_context; }

		void set_clear_color(const glm::vec4& color)
		{
			m_clearColor = color;
		}

		VkDescriptorSet setup_bind_groups(Texture* texture);
		void reload_pipeline();

		SpriteDefinition* allocate_sprite();
		void free_sprite(SpriteDefinition* sprite);
		std::size_t get_sprite_count() const;

		void render_texture(Texture* tex, glm::vec2 position);
		void render_texture(Texture* tex, glm::vec2 position, rectf region);
		void render_texture(Texture* tex, glm::vec2 position, float r, rectf region, float z, glm::vec4 color, glm::vec2 scale);

		// Optional overloads that accept a z-index for depth ordering
		void render_texture(Texture* tex, glm::vec2 position, float z);
		void render_texture(Texture* tex, glm::vec2 position, rectf region, float z);

	private:
		// Rendering
		void handle_render_pass(VkCommandBuffer commandBuffer, double delta);
		void render_gui(VkCommandBuffer commandBuffer, double delta);
		void run_vulkan_render_pass(double delta);
		void ensure_uniform_capacity(uint32_t required);
		bool create_swapchain();
		bool create_render_pass();
		bool create_framebuffers();
		bool create_sprite_pipeline();
		bool create_sync_objects();
		void destroy_swapchain();
		VkShaderModule load_shader(const std::string& path) const;
		void create_descriptor_resources();
		void create_depth_resources();

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

		// Vulkan
		std::shared_ptr<VulkanContext> m_context;
		VkSwapchainKHR m_swapchain{ VK_NULL_HANDLE };
		VkFormat m_surfaceFormat{ VK_FORMAT_UNDEFINED };
		VkFormat m_depthFormat{ VK_FORMAT_D32_SFLOAT };
		VkExtent2D m_swapchainExtent{};
		std::vector<VkImage> m_swapchainImages;
		std::vector<VkImageView> m_swapchainImageViews;
		VkRenderPass m_renderPass{ VK_NULL_HANDLE };
		std::vector<VkFramebuffer> m_framebuffers;
		VkImage m_depthImage{ VK_NULL_HANDLE };
		VkDeviceMemory m_depthMemory{ VK_NULL_HANDLE };
		VkImageView m_depthView{ VK_NULL_HANDLE };
		VkDescriptorSetLayout m_spriteSetLayout{ VK_NULL_HANDLE };
		VkDescriptorSetLayout m_viewportSetLayout{ VK_NULL_HANDLE };
		VkPipelineLayout m_pipelineLayout{ VK_NULL_HANDLE };
		VkPipeline m_pipeline{ VK_NULL_HANDLE };
		VkDescriptorPool m_spriteDescriptorPool{ VK_NULL_HANDLE };
		VkDescriptorSet m_viewportDescriptorSet{ VK_NULL_HANDLE };
		VkBuffer m_spriteBuffer{ VK_NULL_HANDLE };
		VkDeviceMemory m_spriteMemory{ VK_NULL_HANDLE };
		void* m_spriteMapped{ nullptr };
		VkBuffer m_viewportBuffer{ VK_NULL_HANDLE };
		VkDeviceMemory m_viewportMemory{ VK_NULL_HANDLE };
		VkSampler m_sampler{ VK_NULL_HANDLE };
		VkCommandBuffer m_commandBuffer{ VK_NULL_HANDLE };
		VkSemaphore m_imageAvailable{ VK_NULL_HANDLE };
		VkSemaphore m_renderFinished{ VK_NULL_HANDLE };
		VkFence m_inFlight{ VK_NULL_HANDLE };
		glm::vec4 m_clearColor{ 0.0f, 0.0f, 0.0f, 1.0f };
		std::unordered_map<Texture*, VkDescriptorSet> m_textureBindGroups;

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
		std::unordered_map<Texture*, std::vector<const SpriteDefinition*>> m_spriteGroups;
		std::unordered_map<Texture*, std::vector<const SpriteDefinition*>> m_pendingSpriteGroups;
		std::vector<SpriteGpuData> m_spriteUploadData;
		std::vector<SpriteGroupRange> m_spriteGroupRanges;
		size_t m_uniformStride{ SpriteGpuData::gpuDataSize };
		uint32_t m_uniformsCapacity{ 1024 };
	};
}