#include "axeng.h"
#include "imgui.h"

void ax::init()
{
	ax::setup_glfw();
	ax::lua::bind_all();
}

void ax::teardown()
{
	ax::teardown_glfw();
}

ax::Error ax::run_from_directory(std::string_view rootDirectory)
{
	ax::init();
	{
		//
		// Setup module
		//
		auto application{ ax::Application::from_directory(rootDirectory) };
		auto loaded{ application.try_load() };

		if (!loaded)
			return Error::ApplicationLoadFailed;

		// Setup event handlers
		double time{ 0.0 };
		application.window()->get_update_event_handler().subscribe
		(
			[&application, &time](const ax::WindowUpdateEvent& e)
			{
				time += e.delta;
				wgpu::Color clearColor{ std::sin(time), std::cos(time), 0.0, 1.0 };
				application.window()->set_clear_color(clearColor);

				//script->run(env);
			}
		);

		application.window()->get_pre_render_event_handler().subscribe
		(
			[&application](const ax::WindowPreRenderEvent& e)
			{
				static bool flip{ false };
				static double tmr{ 1.0 };
				tmr -= e.delta;

				if (tmr < 0.0)
				{
					tmr = 1.0;
					flip = !flip;
				}

				application.window()->setup_bind_groups(application.textures().get(flip ? "icon" : "tower")->view());
			}
		);

		application.window()->get_render_event_handler().subscribe
		(
			[](const ax::WindowRenderEvent& e)
			{
				e.pass.Draw(4, 1, 0, 0);
			}
		);

		application.window()->get_ui_event_handler().subscribe
		(
			[](const ax::WindowUIEvent& e)
			{
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

		ax::debug::View::register_debug_view(application);

		// Run main loop
		application.window()->run_loop();
	}
	ax::teardown();

	return Error::Success;
}
