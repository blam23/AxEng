#include "axeng.h"
#include "imgui.h"

void ax::init()
{
	ax::lua::bindings::setup();
	ax::setup_glfw();
}

void ax::teardown()
{
	ax::teardown_glfw();
}

ax::Error ax::run(const std::vector<std::string>& args, Application&& app)
{
	ax::init();
	{
		auto loaded{ app.try_load(args) };

		if (!loaded)
			return Error::ApplicationLoadFailed;

		if (app.has_window())
		{
			ax::debug::View::register_debug_view(app);
			app.window()->run_loop();
		}

		app.cleanup();
	}
	ax::teardown();

	return Error::Success;
}

ax::Error ax::run_from_directory(const std::vector<std::string>& args, std::string_view rootDirectory)
{
	spdlog::info("<Ax> Running from directory: '{}'", rootDirectory);
	return run(args, ax::Application::from_directory(rootDirectory));
}

ax::Error ax::run_from_zip(const std::vector<std::string>& args, std::string_view zip)
{
	spdlog::info("<Ax> Running from zip: '{}'", zip);
	return run(args, ax::Application::from_zip(zip));
}
