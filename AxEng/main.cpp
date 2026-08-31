#include <windows.h>

#include "module.h"
#include "resource_loader.h"
#include "window.h"

#include <numbers>

#include <imgui.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Logging
#include "spdlog/spdlog.h"

int main()
{
	// TODO: parse args

#ifdef _DEBUG
	spdlog::set_level(spdlog::level::debug);
#endif

	ax::setup_glfw();
	{
		ax::Module gameModule(ax::ModuleDefinition{
			.name = "Editor",
			.moduleType = ax::ModuleType::Application,
			.loader = std::make_unique<ax::FileResourceLoader>("D:\\axeng")
		});

		ax::FileResourceLoader test("D:\\axeng");
		const auto image_data{ test.load("shipBeige_manned.png") };

		int width, height, channels;
		void* data{ nullptr };
		data = stbi_load_from_memory(image_data.data(), image_data.size(), &width, &height, &channels, 0);

		// Setup window
		ax::Window window({
			.width = 1920,
			.height = 1080,
			.title = "AxEng",
			.vsync = true,
		});
		window.init_wegbpu();
		window.init_imgui();

		// Setup event handlers
		double time{ 0.0 };
		window.get_update_event_handler().subscribe(
			[&window, &time](ax::WindowUpdateEvent& e) {
				time += e.delta;
				wgpu::Color clearColor{ std::sin(time), std::cos(time), 0.0, 1.0};
				window.set_clear_color(clearColor);
			}
		);

		window.get_render_event_handler().subscribe(
			[](ax::WindowRenderEvent& e) {
				e.pass.Draw(3, 1, 0, 0);
			}
		);

		window.get_ui_event_handler().subscribe(
			[](ax::WindowUIEvent& e) {
				ImGui::Begin("Random Stuff");
				{
					static float deltaTimes[512]{ 0 };
					static std::size_t deltaPtr = 0;
					deltaTimes[deltaPtr++] = (float)e.delta * 1000.0f;
					deltaPtr %= 512;
					ImGui::PlotLines("Delta Times (ms)", deltaTimes, 512);
				}
				ImGui::End();
			}
		);

		// Run main loop
		window.run_loop();
	}
	ax::teardown_glfw();
}
