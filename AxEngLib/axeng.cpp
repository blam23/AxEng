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
	spdlog::info("<Ax> Running from directory: '{}'", rootDirectory);

	ax::init();
	{
		//
		// Setup module
		//
		auto application{ ax::Application::from_directory(rootDirectory) };
		auto loaded{ application.try_load() };

		if (!loaded)
			return Error::ApplicationLoadFailed;

		ax::debug::View::register_debug_view(application);

		// Run main loop
		application.window()->run_loop();
	}
	ax::teardown();

	return Error::Success;
}
