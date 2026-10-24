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

ax::Error ax::run
(
	const std::vector<std::string>& args,
	Application&& app,
	wgpu::BackendType backendType,
	const std::filesystem::path& userDirectory,
	const std::optional<audio::AudioSystem::Config>& audioConfig
)
{
	if (!userDirectory.empty())
		app.set_user_directory(userDirectory);
	if (audioConfig)
		app.set_audio_config(*audioConfig);
	ax::init();
	{
		auto loaded{ app.try_load(args, backendType) };

		if (!loaded)
		{
			// Release the window (and everything else) while GLFW is still initialised.
			app.cleanup();
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

ax::Error ax::run_from_directory
(
	flag_set<lua::Permission> permissions,
	const std::vector<std::string>& args,
	std::string_view rootDirectory,
	wgpu::BackendType backendType,
	std::string_view sourceDirectory,
	std::unique_ptr<NativeApplicationModule> nativeModule,
	std::filesystem::path userDirectory,
	std::optional<audio::AudioSystem::Config> audioConfig
)
{
	std::filesystem::path rootPath{ rootDirectory };
	spdlog::info("<Ax> Running from directory: '{}'", std::filesystem::absolute(rootPath).string());
	return run
	(
		args,
		ax::Application::from_directory(permissions, rootPath.string(), sourceDirectory, std::move(nativeModule)),
		backendType,
		userDirectory,
		audioConfig
	);
}

ax::Error ax::run_from_zip
(
	flag_set<lua::Permission> permissions,
	const std::vector<std::string>& args,
	std::string_view zip,
	wgpu::BackendType backendType,
	std::unique_ptr<NativeApplicationModule> nativeModule,
	std::filesystem::path userDirectory,
	std::optional<audio::AudioSystem::Config> audioConfig
)
{
	spdlog::info("<Ax> Running from zip: '{}'", zip);
	return run(args, ax::Application::from_zip(permissions, zip, std::move(nativeModule)), backendType, userDirectory, audioConfig);
}
