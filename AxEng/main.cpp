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

int main()
{
	// TODO: parse args

#ifdef _DEBUG
	spdlog::set_level(spdlog::level::debug);
#endif

	ax::FileSystemModuleDefinition moduleDef
	{
		.name = "Editor",
		.moduleType = ax::ModuleType::Application,
		.loader = { "D:\\axeng" }
	};
	ax::FileSystemModule gameModule{ moduleDef };
	
	ax::lua::global_setup(gameModule.loader());
	ax::lua::init_current_thread();
	auto env{ ax::lua::create_env() };
	auto script{ ax::lua::ScriptManager::load(gameModule.loader(), "Test Script", "test.lua") };

	ax::setup_glfw();
	{
		const auto image_data{ gameModule.loader().load("shipBeige_manned.png") };

		int width, height, channels;
		void* data{ nullptr };
		data = stbi_load_from_memory(image_data.data(), (int)image_data.size(), &width, &height, &channels, 0);

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
			[&window, &time, script, &env](ax::WindowUpdateEvent& e) {
				time += e.delta;
				wgpu::Color clearColor{ std::sin(time), std::cos(time), 0.0, 1.0};
				window.set_clear_color(clearColor);

				script->run(env);
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
					ImGui::PlotLines("Delta Times (ms)", deltaTimes, 512);
				}
				ImGui::End();
			}
		);


		// Run main loop
		window.run_loop();
	}
	ax::teardown_glfw();

	// Specifically unload all scripts before the lua environments are destroyed.
	// TODO: This sucks
	ax::lua::ScriptManager::unload_all();
}
