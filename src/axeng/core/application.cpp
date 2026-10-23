#include "axeng/core/application.h"
#include "axeng/core/msdf_font.h"
#include "axeng/core/native_application_module.h"

#include "axeng/core/lua/bindings/lua_application_bindings.h"
#include "axeng/core/lua/bindings/lua_texture_bindings.h"
#include "axeng/core/lua/external/lua_lib_loader.h"
#include "axeng/core/font_data.h"
#include "axeng/core/font_sheet.h"

ax::Application ax::Application::from_directory
(
	flag_set<lua::Permission> permissions,
	std::string_view root,
	std::string_view sourceRoot,
	std::unique_ptr<NativeApplicationModule> nativeModule
)
{
	const std::filesystem::path sourcePath{ sourceRoot.empty() ? root : sourceRoot };
	return { permissions, DirectoryResourceLoader{ root }, std::filesystem::absolute(sourcePath), std::move(nativeModule) };
}

ax::Application ax::Application::from_embedded
(
	flag_set<lua::Permission> permissions,
	EmbeddedResourceLayout&& layout,
	std::unique_ptr<NativeApplicationModule> nativeModule
)
{
	return { permissions, std::move(layout), {}, std::move(nativeModule) };
}

ax::Application ax::Application::from_zip
(
	flag_set<lua::Permission> permissions,
	std::string_view zipFile,
	std::unique_ptr<NativeApplicationModule> nativeModule
)
{
	return { permissions, ZipResourceLoader{ zipFile }, {}, std::move(nativeModule) };
}

bool ax::Application::init_window(wgpu::BackendType backendType)
{
	LogTimer _timer{ "wgpu initial setup" };

	const auto& manifest{ m_env["manifest"] };
	const auto& window{ manifest["window"] };
	m_window = std::make_unique<Window>(WindowDefinition
	{
		.width = window["width"],
		.height = window["height"],
		.title = window["title"],
		.vsync = window["vsync"],
		.resizable = window["resizable"].get_or(true),
	});

	bool success{ true };
	success = m_window->init_webgpu(backendType);
	if (!success)
		return false;

	m_textures.set_device(m_window->device());

	success = m_window->init_imgui();
	return success;
}

ax::Error ax::Application::add_thread_bindings(sol::state& state)
{
	auto background_table{ state.create_table() };

	background_table["run_script"] =
		[this](const std::string& script_name)
		{
			auto script{ m_scripts.get(script_name) };
			if (script == nullptr)
			{
				spdlog::error("Script '{}' not found", script_name);
				return;
			}
			auto task{ m_backgroundWorker.create_script_task(script) };
			m_backgroundWorker.enqueue(task);
		};

	state["bg"] = background_table;
	bind_native_tasks(state, background_table, *m_nativeTasks, m_backgroundWorker);
	return ax::Error::Success;
}

ax::Error ax::Application::initialise_background_worker(const std::vector<std::string>& args)
{
	std::map<std::string, std::string> sources;
	m_scripts.for_each([&sources](const std::string& name, const lua::Script& script)
	{
		sources.emplace(name, script.code());
	});
	m_nativeTasks = std::make_unique<NativeTaskService>(m_allowedPermissions, std::move(sources));
	m_backgroundWorker.set_thread_hooks(
		[this]() { m_nativeTasks->initialize(); },
		[this]() { m_nativeTasks->finalize(); });
	auto& state{ m_backgroundWorker.lua().state() };
	auto err{ m_backgroundWorker.lua().setup() };
	if (err != ax::Error::Success)
		return err;

	lua::bindings::bind_to_state(state);
	lua::bindings::setup_application_bindings(*this, state, false);
	m_backgroundBindingsInitialized = true;
	err = m_backgroundWorker.lua().run_init();
	if (err != ax::Error::Success)
		return err;

	m_backgroundWorker.start(args);
	return ax::Error::Success;
}

void ax::Application::add_manifest_bindings(sol::state&)
{
}

void ax::Application::init_audio(const sol::table& manifest)
{
	// Audio defaults to on for windowed apps; manifest.audio overrides that either way.
	const sol::optional<bool> audioSetting{ manifest["audio"] };
	if (!audioSetting.value_or(m_create_window))
		return;

	m_audio = std::make_unique<audio::AudioSystem>(audio::AudioSystem::config_from_environment());

	const sol::optional<sol::table> musicTable{ manifest["music"] };
	if (!musicTable)
		return;

	for (const auto& entry : *musicTable)
	{
		const auto name{ entry.first.as<std::string>() };
		if (m_music.load(name, entry.second.as<std::string>()) == nullptr)
			spdlog::error("Failed to load music '{}'", name);
	}
}

void ax::Application::shutdown_audio()
{
	// The audio thread reads the music assets, so it must stop before they are released.
	m_audio.reset();
	m_music.unload_all({});
}

std::string make_safe_directory_name(const std::string& name)
{
	std::string safe_name = name;
	for (auto& c : safe_name)
	{
		if (!std::isalnum(c) && c != '_' && c != '-')
			c = '_';
	}
	return safe_name;
}

bool ax::Application::try_load(const std::vector<std::string>& args, wgpu::BackendType backendType)
{
	LogTimer _timer{ "Application Load" };

	ax::lua::libs::register_embedded();
	ax::Resource::setup_loader(m_loader);
	auto setupErr{ m_scripts.setup({}) };
	if (setupErr != ax::Error::Success)
		return false;

	lua::bindings::bind_to_state(m_scripts.state({}));
	m_mainBindingsInitialized = true;
	setupErr = m_scripts.run_init({});
	if (setupErr != ax::Error::Success)
		return false;

	ax::lua::Script* manifest_script{};
	for (const std::string_view manifest_name : { "manifest.lua", "manifest.luac" })
	{
		if (Resource::contains(m_loader, manifest_name))
		{
			manifest_script = m_scripts.load("!manifest", std::string{ manifest_name });
			if (manifest_script)
				break;
		}
	}
	m_env = m_scripts.create_env();

	auto lua_args{ m_scripts.state({}).create_table(static_cast<int>(args.size()), 0) };
	for (const auto& arg : args)
		lua_args.add(arg);
	m_env["args"] = lua_args;

	add_manifest_bindings(m_scripts.state({}));

	if (!manifest_script)
	{
		spdlog::error("Failed to load manifest");
		return false;
	}

	const auto& res{ manifest_script->run(m_env) };

	if (!res.valid())
	{
		const sol::error msg = res;
		spdlog::error("Failed to parse manifest: {}", msg.what());
		return false;
	}

	ax::lua::Script* stdlib{ m_scripts.load("@std", "@std")};
	const auto& std_res{ stdlib->run(m_env) };
	if (!std_res.valid())
	{
		const sol::error msg = std_res;
		spdlog::error("Failed to load @std lib: {}", msg.what());
		return false;
	}

	auto manifest{ m_env["manifest"] };
	m_name = manifest["name"];
	m_directory_name = make_safe_directory_name(m_name);
	spdlog::info("Application '{}' will use directory '{}'", m_name, m_directory_name);
	m_userFileManager.set_application(*this);

	const sol::table& permissions{ manifest["permissions"].get<sol::table>() };
	for (const auto& entry : permissions)
	{
		const auto perm{ entry.second.as<std::string>() };
		const auto perm_enum{ ax::lua::get_perm_from_string(perm) };
		if (!m_allowedPermissions[perm_enum])
		{
			spdlog::error("Manifest requests permission '{}' which is not allowed", perm);
			return false;
		}
	}
	
	const auto& headless_res{ manifest["headless"] };
	if (headless_res.valid())
		m_create_window = !headless_res.get<bool>();

	const auto& window_manifest{ manifest["window"] };
	if (m_create_window)
	{
		init_window(backendType);

		const auto fontData{ main_font_data() };
		const auto fontResult{ generate_main_font_sheet(fontData, m_fontSheet) };
		if (fontResult != Error::Success)
		{
			spdlog::error("Failed to generate the Source Code Pro font sheet.");
			return false;
		}

		std::vector<uint8_t> encodedFont{};
		const auto encodeResult{ encode_png(
			font_sheet_width, font_sheet_height, 4, m_fontSheet.pixels.data(), font_sheet_width * 4, encodedFont) };
		if (encodeResult != Error::Success)
		{
			spdlog::error("Failed to encode the generated Source Code Pro font sheet.");
			return false;
		}

		if (m_textures.load_from_raw("__builtin_font", "QuirkyRobot.ttf", encodedFont) == nullptr)
		{
			spdlog::error("Failed to load the generated Source Code Pro font sheet.");
			return false;
		}

		if (generate_msdf_font(fontData, m_msdfFont) == Error::Success)
		{
			std::vector<uint8_t> encodedMsdf{};
			auto* msdfTexture{ encode_png(m_msdfFont.width, m_msdfFont.height, 4, m_msdfFont.pixels.data(), m_msdfFont.width * 4, encodedMsdf) == Error::Success
				? m_textures.load_from_raw(std::string{ sdf_font_texture_name }, "QuirkyRobot.ttf", encodedMsdf)
				: nullptr };
			if (msdfTexture != nullptr)
				msdfTexture->set_msdf(true);
			else
				spdlog::error("Failed to load the generated MSDF font.");
		}
		else
			spdlog::error("Failed to generate the MSDF font.");

		const sol::table& textures{ manifest["textures"].get<sol::table>() };
		for (const auto& entry : textures)
		{
			const auto name{ entry.first.as<std::string>() };
			if (!is_font_texture_name(name))
				m_textures.load(name, entry.second.as<std::string>());
		}
	}

	if (m_create_window)
	{
		const auto& icon{ m_textures.get(window_manifest["icon"]) };
		if (icon)
		{
			const auto img { icon->create_glfw_image() };
			glfwSetWindowIcon(m_window->glfw_handle(), 1, &img);
		}
	}

	init_audio(manifest.get<sol::table>());

	if (manifest["atlas_texture"].valid())
	{
		const auto atlas_texture{ manifest["atlas_texture"].get<std::string>() };
		if (atlas_texture != "null")
			m_atlasTexture = m_textures.get(atlas_texture);

		const sol::table& atlas_regions{ manifest["texture_atlas"].get<sol::table>() };
		for (const auto& entry : atlas_regions)
		{
			const auto subTexture{ entry.first.as<std::string>() };
			const auto regionTable{ entry.second.as<sol::table>() };
			rectf region { regionTable[1], regionTable[2], regionTable[3], regionTable[4] };
			if (m_atlasTexture)
				m_atlasTexture->add_region(subTexture, region);
		}

		spdlog::info("Loaded texture atlas with {} regions", atlas_regions.size());
	}

	const sol::table& scripts{ manifest["scripts"].get<sol::table>() };
	for (const auto& entry : scripts)
		m_scripts.load(entry.first.as<std::string>(), entry.second.as<std::string>());

	//const sol::table& types{ manifest["types"].get<sol::table>() };
	//for (const auto& entry : types)
	//	m_typeGen.register_type(entry.first.as<std::string>(), entry.second.as<ax::type::TypeDef>());

	std::string entryPointScript = manifest["entry_point"];
	m_entryPoint = m_scripts.get(entryPointScript);

	if (!m_entryPoint)
	{
		spdlog::error("Failed to load entry point script: '{}'", entryPointScript);
		return false;
	}

	lua::bindings::setup_application_bindings(*this, m_scripts.state({}), true);
	if (m_nativeModule)
	{
		m_nativeModuleBound = true;
		const auto bindErr{ m_nativeModule->bind_lua(*this, m_scripts.state({})) };
		if (bindErr != ax::Error::Success)
		{
			spdlog::error("Failed to bind native application module: {}", ax::get_error_name(bindErr));
			return false;
		}
	}

	//
	// Main-thread specific bindings
	//
	sol::table app{ m_scripts.state({})["app"].get<sol::table>() };
	app["run_in_this_environment"] =
		[this](const sol::table& script) -> sol::object
		{
			if (script["valid"])
			{
				auto ptr{ script["ptr"].get<ax::lua::Script*>() };
				if (ptr == nullptr)
				{
					spdlog::error("Invalid script object, cannot run");
					return nullptr;
				}
				auto res{ ptr->run_no_cache(m_env) };
				if (!res.valid())
				{
					const sol::error msg = res;
					spdlog::error("Failed to run script: {}", msg.what());
				}
				return res;
			}
			else
			{
				spdlog::error("Invalid script, cannot run");
				return nullptr;
			}
		};

	const bool hasThreadPermission{ m_allowedPermissions[ax::lua::Permission::Threads] };
	if (m_create_window || hasThreadPermission)
	{
		const auto threadErr{ initialise_background_worker(args) };
		if (threadErr != ax::Error::Success)
			return false;
	}
	if (hasThreadPermission)
	{
		const auto bindingsErr{ add_thread_bindings(m_scripts.state({})) };
		if (bindingsErr != ax::Error::Success)
			return false;
	}

	if (m_nativeModule)
	{
		const auto initializeErr{ m_nativeModule->initialize(*this) };
		if (initializeErr != ax::Error::Success)
		{
			spdlog::error("Failed to initialize native application module: {}", ax::get_error_name(initializeErr));
			return false;
		}
	}

	spdlog::info("<Lua> Running entry point script: '{}'", entryPointScript);
	const auto ep_res = m_entryPoint->run(m_env);
	if (!ep_res.valid())
	{
		const sol::error msg = ep_res;
		spdlog::error("Failed to run entry point script: {}", msg.what());
		return false;
	}
	else if(const auto err = ep_res.get<ax::Error>(); err != ax::Error::Success)
	{
		spdlog::error("Entry point script failed: {}", ax::get_error_name(err));
		return false;
	}

	m_loaded = true;
	return m_loaded;
}

void ax::Application::cleanup()
{
	cleanup_native_module();
	if (m_nativeTasks)
		m_nativeTasks->stop();
	m_backgroundWorker.stop();
	cleanup_bindings();
	shutdown_audio();

	{
		std::lock_guard lock{ m_shared_mutex };
		m_shared.clear();
	}

	m_scripts.cleanup({});

	if (m_window)
		m_window = nullptr;

	{
		std::lock_guard lock{ m_tempTexturesMutex };
		for (const auto& entry : m_tempTextures)
			entry.second->active.store(false, std::memory_order_relaxed);
		m_tempTextures.clear();
	}

	ax::Resource::cleanup_loader(m_loader);
}

ax::Application::~Application()
{
	if (m_nativeTasks)
		m_nativeTasks->stop();
	m_backgroundWorker.stop();

	if (m_loaded)
		cleanup();
	else
	{
		cleanup_native_module();
		cleanup_bindings();
		shutdown_audio();
		m_window.reset();
		std::lock_guard lock{ m_tempTexturesMutex };
		for (const auto& entry : m_tempTextures)
			entry.second->active.store(false, std::memory_order_relaxed);
		m_tempTextures.clear();
	}

	m_scripts.cleanup({});
}

void ax::Application::cleanup_bindings()
{
	if (m_mainBindingsInitialized)
	{
		lua::bindings::cleanup_state(*this, m_scripts.state({}));
		m_mainBindingsInitialized = false;
	}
	if (m_backgroundBindingsInitialized)
	{
		lua::bindings::cleanup_state(*this, m_backgroundWorker.lua().state());
		m_backgroundBindingsInitialized = false;
	}
}

void ax::Application::cleanup_native_module()
{
	if (m_nativeModule && m_nativeModuleBound)
	{
		const auto err{ m_nativeModule->shutdown(*this) };
		if (err != ax::Error::Success)
			spdlog::error("Failed to shut down native application module: {}", ax::get_error_name(err));
		m_nativeModuleBound = false;
	}
}

void ax::Application::call_deferred(std::function<void()> func)
{
	m_window->call_deferred(std::move(func));
}

void ax::Application::register_conditional_binding
(
	const std::string &name,
	Predicate&& predicate,
	BindingCallback&& binding
)
{
	m_conditionalBindings[name] = { std::forward<Predicate>(predicate), std::forward<BindingCallback>(binding) };
}

ax::Application::Application
(
	flag_set<lua::Permission> permissions,
	ResourceLoader&& loader,
	std::filesystem::path projectSourceDirectory,
	std::unique_ptr<NativeApplicationModule> nativeModule
)
	: m_nativeModule{ std::move(nativeModule) }
	, m_loader{ std::move(loader) }
	, m_projectSourceDirectory{ std::move(projectSourceDirectory) }
	, m_scripts{ Badge<Application>{}, permissions, m_loader }
	, m_textures{ Badge<Application>{}, m_loader }
	, m_music{ Badge<Application>{}, m_loader }
	, m_allowedPermissions{ permissions }
	, m_backgroundWorker{ true, permissions }
{
}
