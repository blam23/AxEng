#include <windows.h>

#include "forward.h"
#include "module.h"
#include "resource_loader.h"
#include "log_timer.h"
#include "lua_engine.h"
#include "script.h"
#include "window.h"

#include <numbers>

#include <imgui.h>

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
		return 1;
	}

	ax::setup_glfw();
	{
		//
		// Setup module
		//
		auto application{ ax::Module::from_directory(rootDirectory) };
		auto loaded{ application.try_load() };

		if (!loaded)
			return 2;

		// Setup event handlers
		double time{ 0.0 };
		application.window()->get_update_event_handler().subscribe
		(
			[&application, &time](const ax::WindowUpdateEvent& e) {
				time += e.delta;
				wgpu::Color clearColor{ std::sin(time), std::cos(time), 0.0, 1.0};
				application.window()->set_clear_color(clearColor);

				//script->run(env);
			}
		);

		application.window()->get_render_event_handler().subscribe
		(
			[](const ax::WindowRenderEvent& e) {
				e.pass.Draw(3, 1, 0, 0);
			}
		);

		application.window()->get_ui_event_handler().subscribe
		(
			[](const ax::WindowUIEvent& e) {
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
		application.window()->run_loop();
	}

	ax::teardown_glfw();
}
