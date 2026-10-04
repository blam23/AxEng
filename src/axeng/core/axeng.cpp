#include "axeng/core/axeng.h"
#include "imgui.h"
#include "axeng/core/lua/external/lua_libs.h"

void ax::init()
{
	ax::lua::libs::load_all_embedded();
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

ax::Error ax::run_from_directory(flag_set<lua::Permission> permissions, const std::vector<std::string>& args, std::string_view rootDirectory)
{
	std::filesystem::path rootPath{ rootDirectory };
	spdlog::info("<Ax> Running from directory: '{}'", std::filesystem::absolute(rootPath).string());
	return run(args, ax::Application::from_directory(permissions, rootPath.string()));
}

ax::Error ax::run_from_zip(flag_set<lua::Permission> permissions, const std::vector<std::string>& args, std::string_view zip)
{
	spdlog::info("<Ax> Running from zip: '{}'", zip);
	return run(args, ax::Application::from_zip(permissions, zip));
}
