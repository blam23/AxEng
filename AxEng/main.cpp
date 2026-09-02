#include <windows.h>

#include "module.h"
#include "resource_loader.h"
#include "log_timer.h"
#include "lua_engine.h"
#include "script.h"
#include "window.h"

#include <numbers>

#include <imgui.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "argparse/argparse.hpp"

int main(int argc, char* argv[])
{
#ifdef _DEBUG
	spdlog::set_level(spdlog::level::debug);
#endif

	//
	// Parse Args
	//

	argparse::ArgumentParser program("AxEng");

	std::string rootDirectory;
	program.add_argument("-r", "--root")
		.store_into(rootDirectory)
		.required()
		.help("Loads an application from the given root directory");

	//std::string zipPath;
	//program.add_argument("-z", "--zip")
	//	.store_into(zipPath)
	//	.help("Loads an application from the given zip file");

	try
	{
		program.parse_args(argc, argv);
	}
	catch (const std::exception& err)
	{
		spdlog::error("Failed to parse arguments: {}", err.what());
		spdlog::error("{}", program.help().str());
	}

	//
	// Setup module
	//

	ax::DirectoryModule gameModule(rootDirectory);
	gameModule.try_load();

	ax::setup_glfw();
	{
		//const auto image_data{ gameModule.loader().load("shipBeige_manned.png") };

		//int width, height, channels;
		//void* data{ nullptr };
		//data = stbi_load_from_memory(image_data.data(), (int)image_data.size(), &width, &height, &channels, 0);

		// Setup window
		ax::Window window
		({
			.width = 1920,
			.height = 1080,
			.title = "AxEng",
			.vsync = true,
		});
		window.init_wegbpu();
		window.init_imgui();

		// Setup event handlers
		double time{ 0.0 };
		window.get_update_event_handler().subscribe
		(
			[&window, &time](ax::WindowUpdateEvent& e) {
				time += e.delta;
				wgpu::Color clearColor{ std::sin(time), std::cos(time), 0.0, 1.0};
				window.set_clear_color(clearColor);

				//script->run(env);
			}
		);

		window.get_render_event_handler().subscribe
		(
			[](ax::WindowRenderEvent& e) {
				e.pass.Draw(3, 1, 0, 0);
			}
		);

		window.get_ui_event_handler().subscribe
		(
			[](ax::WindowUIEvent& e) {
				ImGui::Begin("Random Stuff");
				{
					static float deltaTimes[512]{ 0 };
					static std::size_t deltaPtr = 0;
					deltaTimes[deltaPtr++] = (float)e.delta * 1000.0f;
					deltaPtr %= 512;
					ImGui::PlotHistogram("Delta Times (ms)", deltaTimes, 512);
				}
				ImGui::End();
			}
		);


		// Run main loop
		window.run_loop();
	}
	ax::teardown_glfw();
}
