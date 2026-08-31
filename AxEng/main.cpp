#include "module.h"
#include "window.h"

#include <numbers>

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
		ax::Module gameModule;

		ax::Window window({
			.width = 1920,
			.height = 1080,
			.title = "AxEng",
			.vsync = true,
		});

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

		window.init_wegbpu();
		window.init_imgui();

		window.run_loop();
	}
	ax::teardown_glfw();
}
