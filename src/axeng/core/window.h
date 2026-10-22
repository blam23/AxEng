#pragma once

// Windows
#include <windows.h>

// STD
#include <deque>
#include <filesystem>
#include <future>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

// GLFW
#include "axeng/external/glfw3webgpu.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// GFX
#include <webgpu/webgpu_cpp.h>
#include <webgpu/webgpu_cpp_print.h>

// AxEng
#include "axeng/core/helpers.h"
#include "axeng/core/event.h"
#include "axeng/debug/debug_view.h"
#include "axeng/core/camera.h"
#include "axeng/core/texture.h"
#include "axeng/core/retained_sprites.h"
#include "axeng/core/shapes/shape.h"
#include "axeng/core/window_events.h"

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
		bool resizable{ true };
	};

	struct WindowFrameImageEvent
	{
		bool success{ false };
		bool compared{ false };
		bool matches{ false };
		uint32_t differingPixels{ 0 };
		uint32_t pixelCount{ 0 };
		uint32_t maxChannelDifference{ 0 };
		std::string message{};
		std::string imageData{};
	};

	class Window
	{
	public:
		DISABLE_COPY_AND_MOVE(Window);

		struct PendingDrawBatch
		{
			Texture* target{ nullptr };
			std::shared_ptr<Texture> targetLease{};
			ShapeList shapes;
			std::vector<SpriteDefinition> textures;
		};

		Window(const WindowDefinition&);
		~Window();

		static void resize_event_handler(GLFWwindow* window, int width, int height);


		bool init_webgpu(wgpu::BackendType backendType = wgpu::BackendType::Undefined);
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

		using ResizeEventHandler = EventHandler<WindowResizeEvent>;
		ResizeEventHandler& get_resize_event_handler()
		{
			return m_resizeEventHandler;
		}

		using FrameImageEventHandler = EventHandler<WindowFrameImageEvent>;
		FrameImageEventHandler& get_frame_image_event_handler()
		{
			return m_frameImageEventHandler;
		}

		bool capture_next_frame(const std::filesystem::path& path);
		bool compare_next_frame(Texture& reference, std::shared_ptr<Texture> referenceLease = {});

		wgpu::Device& device() noexcept { return m_device; }
		const wgpu::Device& device() const noexcept { return m_device; }
		wgpu::Queue& queue() noexcept { return m_queue; }
		const wgpu::Queue& queue() const noexcept { return m_queue; }

		std::uint32_t width() const noexcept { return m_width; }
		std::uint32_t height() const noexcept { return m_height; }
		Camera& camera() noexcept { return m_camera; }
		const Camera& camera() const noexcept { return m_camera; }
		// Legacy naming: global is screen pixels; viewport is world coordinates.
		// Rectangles use (x, y, width, height).
		glm::vec2 global_to_viewport(const glm::vec2& position) const;
		glm::vec4 global_to_viewport(const glm::vec4& rect) const;
		glm::vec2 viewport_to_global(const glm::vec2& position) const;
		glm::vec4 viewport_to_global(const glm::vec4& rect) const;
		// Tests world-space overlap with the screen, including touching edges.
		bool screen_contains_region(const glm::vec4& rect) const;

		bool vsync() const noexcept { return m_vsync; }
		// Must call create_surfaces() after changing vsync
		void set_vsync(bool enabled) noexcept { m_vsync = enabled; }
		bool msaa_enabled() const noexcept { return m_sampleCount == 4; }
		void set_msaa_enabled(bool enabled);

		void set_clear_color(const wgpu::Color& color)
		{
			m_clearColor = color;
		}

		wgpu::BindGroup setup_bind_groups(const wgpu::TextureView& view);
		wgpu::BindGroup setup_bind_groups(const wgpu::TextureView& view, const wgpu::Buffer& buffer);
		void reload_pipeline();
		bool create_surfaces();

		SpriteDefinition* allocate_sprite();
		SpriteDefinition* allocate_group_sprite(const std::shared_ptr<SpriteGroup>& group);
		std::shared_ptr<SpriteGroup> create_sprite_group();
		void release_sprite_group(const std::shared_ptr<SpriteGroup>& group);
		void transfer_sprite(SpriteDefinition* sprite, const std::shared_ptr<SpriteGroup>& group);
		std::shared_ptr<StaticSpriteBatch> attach_sprite_batch(Texture* texture,
			std::shared_ptr<SpriteBuffer> data, glm::vec2 atlasOffset, std::int64_t order);
		void free_sprite(SpriteDefinition* sprite);
		std::size_t get_sprite_count() const;
		std::size_t sprite_allocations() const { return m_spriteAllocations; }
		std::size_t sprite_frees() const { return m_spriteFrees; }
		std::size_t static_uploads() const { return m_staticUploads; }
		std::size_t static_batch_count() const;
		double sprite_setup_deadline() const { return m_spriteSetupDeadline; }
		void invalidate_texture_bind_group(Texture* texture);

		bool submit_pending_draw_batch(PendingDrawBatch batch);
		// Snapshots queued sprites and debug shapes for execution after the active frame passes end.
		void draw_pending();

		void render_texture(Texture* tex, glm::vec2 position, std::shared_ptr<Texture> keepAlive = {});
		void render_texture(Texture* tex, glm::vec2 position, rectf region, std::shared_ptr<Texture> keepAlive = {});
		void render_texture(Texture* tex, glm::vec2 position, float r, rectf region, float z, glm::vec4 color, glm::vec2 scale, std::shared_ptr<Texture> keepAlive = {});
		void render_ui_texture(Texture* tex, glm::vec2 position, std::shared_ptr<Texture> keepAlive = {}, glm::vec4 clipRect = { 0.0f, 0.0f, -1.0f, -1.0f });
		void render_ui_texture(Texture* tex, glm::vec2 position, rectf region, std::shared_ptr<Texture> keepAlive = {}, glm::vec4 clipRect = { 0.0f, 0.0f, -1.0f, -1.0f });
		void render_ui_texture(Texture* tex, glm::vec2 position, float r, rectf region, float z, glm::vec4 color, glm::vec2 scale, std::shared_ptr<Texture> keepAlive = {}, glm::vec4 clipRect = { 0.0f, 0.0f, -1.0f, -1.0f });

		// Optional overloads that accept a z-index for depth ordering
		void render_texture(Texture* tex, glm::vec2 position, float z, std::shared_ptr<Texture> keepAlive = {});
		void render_texture(Texture* tex, glm::vec2 position, rectf region, float z, std::shared_ptr<Texture> keepAlive = {});
		void render_textures(std::vector<SpriteDefinition> sprites);
		void render_ui_texture(Texture* tex, glm::vec2 position, float z, std::shared_ptr<Texture> keepAlive = {}, glm::vec4 clipRect = { 0.0f, 0.0f, -1.0f, -1.0f });
		void render_ui_texture(Texture* tex, glm::vec2 position, rectf region, float z, std::shared_ptr<Texture> keepAlive = {}, glm::vec4 clipRect = { 0.0f, 0.0f, -1.0f, -1.0f });

		void call_deferred(std::function<void()> func);

		bool try_embed_child(DWORD processID);
		void try_kill_child(DWORD processID);
		void set_embedded_child_position(DWORD processID, int x, int y, int width, int height);
		void focus_child(DWORD processID);
		void redirect_input_to_child(DWORD processID);
		void reset_input_redirection();

	private:
		friend struct WindowTestAccess;

		struct FrameImageRequest;

		// Rendering
		void handle_render_pass(wgpu::RenderPassEncoder& pass, double delta);
		void draw_sprite_batches(wgpu::RenderPassEncoder& pass, const wgpu::RenderPipeline& pipeline);
		void stage_sprite_batches();
		void draw_transparent_sprites(wgpu::RenderPassEncoder& pass);
		void draw_pending_batch(const PendingDrawBatch& batch, const wgpu::TextureView& screenView);
		wgpu::BindGroup get_texture_bind_group(Texture* texture);
		void render_gui(wgpu::RenderPassEncoder& pass, double delta);
		void run_wgpu_render_pass(double delta, std::future<wgpu::SurfaceTexture> surfaceFuture);
		WindowFrameImageEvent read_frame_image
		(
			const FrameImageRequest& request,
			wgpu::Buffer& readback,
			uint32_t bytesPerRow
		);
		void ensure_uniform_capacity(uint32_t required);
		void create_composite_bind_group();
		wgpu::RenderPipeline setup_pending_pipeline(wgpu::TextureFormat format);
		void queue_texture(SpriteDefinition sprite);

		// Logic
		void handle_tick(double delta);

		// Properties
		uint32_t m_width;
		uint32_t m_height;
		bool m_vsync;
		std::uint32_t m_sampleCount{ 4 };
		std::uint32_t m_requestedSampleCount{ 4 };
		bool m_can_close{ true };
		Camera m_camera;

		// Events
		UpdateEventHandler m_updateEventHandler;
		PreRenderEventHandler m_preRenderEventHandler;
		RenderEventHandler m_renderEventHandler;
		UIEventHandler m_uiEventHandler;
		RequestCloseEventHandler m_requestCloseEventHandler;
		ResizeEventHandler m_resizeEventHandler;
		FrameImageEventHandler m_frameImageEventHandler;

		// GLFW
		GLFWwindow* m_window{ nullptr };
		void register_window_events();
		static void window_close_handler(GLFWwindow* window);
		static LRESULT CALLBACK raw_windows_event(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

		// WGPU
		wgpu::Adapter m_adapter;
		wgpu::Device m_device;
		wgpu::Queue m_queue;
		wgpu::Surface m_surface;
		wgpu::TextureFormat m_surfaceFormat{};
		bool m_surfaceSupportsCopySource{ false };
		wgpu::Color m_clearColor{ 0.0, 0.0, 0.0, 1.0 };
		wgpu::Texture m_depthTexture;
		wgpu::TextureView m_depthTextureView;
		wgpu::TextureFormat m_depthTextureFormat{ wgpu::TextureFormat::Depth24Plus };
		wgpu::Texture m_multisampleTexture;
		wgpu::ShaderModule m_shader;
		wgpu::ShaderModule m_compositeShader;
		wgpu::RenderPipeline m_pipeline;
		wgpu::RenderPipeline m_oitPipeline;
		wgpu::RenderPipeline m_compositePipeline;
		wgpu::Texture m_oitAccumulationTexture;
		wgpu::TextureView m_oitAccumulationView;
		wgpu::Texture m_oitRevealageTexture;
		wgpu::TextureView m_oitRevealageView;
		wgpu::BindGroupLayout m_compositeGroupLayout;
		wgpu::BindGroup m_compositeBindGroup;
		wgpu::Buffer m_uniforms;
		wgpu::Sampler m_nearestSampler;
		wgpu::Sampler m_linearSampler;
		wgpu::BindGroupLayout m_groupLayout;
		wgpu::BindGroupLayout m_globalLayout;
		wgpu::Buffer m_viewportBuffer;
		wgpu::BindGroup m_viewportBindGroup;
		std::unordered_map<Texture*, wgpu::BindGroup> m_textureBindGroups;
		std::mutex m_textureBindGroupsMutex{};
		std::map<wgpu::TextureFormat, wgpu::RenderPipeline> m_pendingPipelines;

		std::vector<PendingDrawBatch> m_pendingDrawBatches;
		std::mutex m_pendingDrawMutex{};

		struct FrameImageRequest
		{
			Texture* reference{ nullptr };
			std::shared_ptr<Texture> referenceLease{};
			std::filesystem::path outputPath{};
		};
		std::mutex m_frameImageRequestMutex{};
		std::optional<FrameImageRequest> m_frameImageRequest{};

		struct SpriteGroupRange
		{
			Texture* tex;
			uint32_t start;
			uint32_t count;
		};

		std::deque<SpriteDefinition> m_spriteSlots;
		std::vector<SpriteDefinition*> m_activeSprites;
		std::vector<SpriteDefinition*> m_freeSpriteSlots;
		std::vector<std::shared_ptr<SpriteGroup>> m_spriteVisibilityGroups;
		std::vector<std::shared_ptr<StaticSpriteBatch>> m_staticSpriteBatches;
		std::size_t m_spriteAllocations{};
		std::size_t m_spriteFrees{};
		std::size_t m_staticUploads{};
		double m_spriteSetupDeadline{};
		std::uint64_t m_spriteGeneration{};
		std::thread::id m_rendererThread{ std::this_thread::get_id() };
		std::mutex m_pendingTextureMutex{};
		std::vector<SpriteDefinition> m_queuedTextures;
		std::vector<SpriteDefinition> m_pendingTextures;
		std::unordered_map<Texture*, std::vector<const SpriteDefinition*>> m_spriteGroups;
		std::unordered_map<Texture*, std::vector<const SpriteDefinition*>> m_pendingSpriteGroups;
		std::vector<SpriteGpuData> m_spriteUploadData;
		std::vector<SpriteGroupRange> m_spriteGroupRanges;
		size_t m_uniformStride{ SpriteGpuData::gpuDataSize };
		uint32_t m_uniformsCapacity{ 1024 };

		std::mutex m_deferredMutex{};
		std::vector<std::function<void()>> m_deferred{};

		std::map<DWORD, HWND> m_childWindows{};
	};
}