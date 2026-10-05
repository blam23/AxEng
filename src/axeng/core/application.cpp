#include "axeng/core/application.h"

#include "axeng/core/lua/bindings/lua_application_bindings.h"
#include "axeng/core/lua/external/lua_lib_loader.h"

ax::Application ax::Application::from_directory(flag_set<lua::Permission> permissions, std::string_view root)
{
	return { permissions, DirectoryResourceLoader{ root } };
}

ax::Application ax::Application::from_embedded(flag_set<lua::Permission> permissions, EmbeddedResourceLayout&& layout)
{
	return { permissions, std::move(layout) };
}

ax::Application ax::Application::from_zip(flag_set<lua::Permission> permissions, std::string_view zipFile)
{
	return { permissions, ZipResourceLoader{ zipFile } };
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
	});

	bool success{ true };
	success = m_window->init_webgpu(backendType);
	if (!success)
		return false;

	m_textures.set_device(m_window->device());

	success = m_window->init_imgui();
	return success;
}

void ax::Application::add_thread_bindings(sol::state& state, const std::vector<std::string>& args)
{
	initialise_background_worker(args);

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
}

void ax::Application::initialise_background_worker(const std::vector<std::string>& args)
{
	m_backgroundWorker.lua().setup();
	lua::bindings::setup_application_bindings(*this, m_backgroundWorker.lua().state());
	m_backgroundWorker.start(args);
}

void ax::Application::add_manifest_bindings(sol::state&)
{
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
	m_scripts.setup({});

	ax::lua::Script* manifest_script{ m_scripts.load("!manifest", "manifest.luac") };
	m_env = m_scripts.create_env();

	m_env["args"] = args;

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

		const sol::table& textures{ manifest["textures"].get<sol::table>() };
		for (const auto& entry : textures)
			m_textures.load(entry.first.as<std::string>(), entry.second.as<std::string>());
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

	lua::bindings::setup_application_bindings(*this, m_scripts.state({}));

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

	app["thread"] = "main";
	app["on_main_thread"] =
		[]() -> bool
		{
			return true;
		};

	app["signal"] =
		[](lua::SharedObject& obj, const std::string& signal_name)
		{
			obj.signal(signal_name);
		};

	app["on_signal"] =
		[this](lua::SharedObject& obj, const std::string& signal_name, sol::protected_function func)
		{
			obj.on_signal(this, signal_name, func);
		};

	m_scripts.state({}).new_usertype<lua::SharedObject>
		(
			"shared",
			"set", &lua::SharedObject::set,
			"get", [this](lua::SharedObject& ref, const std::string& key) { return ref.get(m_scripts.state({}), key); }
		);

	if (m_allowedPermissions[ax::lua::Permission::Threads])
		add_thread_bindings(m_scripts.state({}), args);

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
	{
		std::lock_guard lock{ m_shared_mutex };
		m_shared.clear();
	}

	m_scripts.cleanup({});

	if (m_window)
		m_window = nullptr;

	ax::Resource::cleanup_loader(m_loader);
}

ax::Application::~Application()
{
	m_backgroundWorker.stop();

	if (m_loaded)
		cleanup();

	m_scripts.cleanup({});
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

ax::Application::Application(flag_set<lua::Permission> permissions, ResourceLoader&& loader)
	: m_loader{ std::move(loader) }
	, m_scripts{ Badge<Application>{}, *this, permissions, m_loader }
	, m_textures{ Badge<Application>{}, m_loader }
	, m_allowedPermissions{ permissions }
	, m_backgroundWorker{ true, *this, permissions }
{
}
