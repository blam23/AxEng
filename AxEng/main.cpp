#include "window.h"
#include "scripting.h"

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
		ax::Window window
		{ 
			ax::WindowDefinition
			{
				.width = 1920,
				.height = 1080,
				.title = "AxEng",
				.vsync = true,
			}
		};
		window.init_wegbpu();
		window.init_imgui();

		window.run_loop();
	}
	ax::teardown_glfw();
}
