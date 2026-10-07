#include "axeng/core/axeng.h"
#include "imgui.h"
#include "axeng/core/lua/external/lua_libs.h"

void ax::init()
{
	// Setup log capturing as early as possible
	debug::ViewManager::setup_console_logging();
	
	ax::lua::libs::load_all_embedded();
	ax::lua::bindings::setup();
	ax::setup_glfw();
}

void ax::teardown()
{
	debug::ViewManager::teardown_console_logging();
	ax::teardown_glfw();
}

ax::Error ax::run(const std::vector<std::string>& args, Application&& app, wgpu::BackendType backendType)
{
	ax::init();
	{
		auto loaded{ app.try_load(args, backendType) };

		if (!loaded)
		{
			ax::teardown();
			return Error::ApplicationLoadFailed;
		}

		if (app.has_window())
		{
			debug::ViewManager::setup(app);
			debug::ViewManager::register_view(GLFW_KEY_F11, debug::ViewManager::debug_view_callback);
			debug::ViewManager::register_view(GLFW_KEY_F10, debug::ViewManager::console_view_callback);
			
			app.window()->run_loop();

			debug::ViewManager::teardown();
		}

		app.cleanup();
	}
	ax::teardown();

	return Error::Success;
}

ax::Error ax::run_from_directory(flag_set<lua::Permission> permissions, const std::vector<std::string>& args, std::string_view rootDirectory, wgpu::BackendType backendType)
{
	std::filesystem::path rootPath{ rootDirectory };
	spdlog::info("<Ax> Running from directory: '{}'", std::filesystem::absolute(rootPath).string());
	return run(args, ax::Application::from_directory(permissions, rootPath.string()), backendType);
}

ax::Error ax::run_from_zip(flag_set<lua::Permission> permissions, const std::vector<std::string>& args, std::string_view zip, wgpu::BackendType backendType)
{
	spdlog::info("<Ax> Running from zip: '{}'", zip);
	return run(args, ax::Application::from_zip(permissions, zip), backendType);
}
