#include "application.h"

ax::Application ax::Application::from_directory(std::string_view root)
{
	return { DirectoryResourceLoader{ root } };
}

ax::Application ax::Application::from_embedded(EmbeddedResourceLayout&& layout)
{
	return { std::move(layout) };
}

ax::Application ax::Application::from_zip(std::string_view zipFile)
{
	return { ZipResourceLoader{ zipFile } };
}

bool ax::Application::init_window()
{
	LogTimer _timer{ "wgpu initial setup" };

	const auto& app{ m_env["app"] };
	const auto& window{ app["window"] };
	m_window = std::make_unique<Window>(WindowDefinition
	{
		.width = window["width"],
		.height = window["height"],
		.title = window["title"],
		.vsync = window["vsync"],
	});

	bool success{ true };
	success = m_window->init_webgpu();
	if (!success)
		return false;

	m_textures.set_device(m_window->device());

	success = m_window->init_imgui();
	return success;
}

void ax::Application::add_application_bindings(sol::state& state)
{
	const auto& app{ m_env["app"] };
	auto on_ui_table{ state.create_table() };

	on_ui_table["subscribe"] = 
		[this](std::function<void(double delta)> f) -> size_t 
		{
			return m_window->get_ui_event_handler().subscribe([f](const ax::WindowUIEvent& e) { f(e.delta); });
		};

	on_ui_table["unsubscribe"] = 
		[this](size_t id)
		{
			m_window->get_ui_event_handler().unsubscribe(id);
		};

	auto resource_lookup_texture{ state.create_table() };


	resource_lookup_texture["get_texture"] =
		[this, &state](const std::string& name) -> sol::table
		{
			auto text{ m_textures.get(name) };

			auto ret = state.create_table();

			if (text)
			{
				ret["valid"] = true;
				ret["view"] = text->view();
				ret["ui_view"] = text->imgui_view();
				ret["width"] = text->width();
				ret["height"] = text->height();
			}
			else
			{
				ret["valid"] = false;
			}

			return ret;
		};

	app["on_ui"] = on_ui_table;
	app["res"] = resource_lookup_texture;
}

void ax::Application::add_manifest_bindings(sol::state&)
{
}

bool ax::Application::try_load()
{
	LogTimer _timer{ "Application Load" };

	ax::Resource::setup_loader(m_loader);
	m_scripts.setup({});

	ax::lua::Script* manifest{ m_scripts.load("!manifest", "manifest.luac") };
	m_env = m_scripts.create_env();

	add_manifest_bindings(m_scripts.state({}));

	if (!manifest) 
	{
		spdlog::error("Failed to load manifest");
		return false;
	}

	const auto& res{ manifest->run(m_env) };

	if (!res.valid())
	{
		const sol::error msg = res;
		spdlog::error("Failed to parse manifest: {}", msg.what());
		return false;
	}

	const auto& app{ m_env["app"] };
	m_name = app["name"];

	init_window();
	const auto& window{ app["window"] };
	window["handle"] = (void*)m_window->glfw_handle();

	const sol::table& textures{ app["textures"].get<sol::table>() };
	for (const auto& entry : textures)
		m_textures.load(entry.first.as<std::string>(), entry.second.as<std::string>());

	const auto& icon{ m_textures.get(window["icon"])->create_glfw_image() };
	glfwSetWindowIcon(m_window->glfw_handle(), 1, &icon);

	const sol::table& scripts{ app["scripts"].get<sol::table>() };
	for (const auto& entry : scripts)
		m_scripts.load(entry.first.as<std::string>(), entry.second.as<std::string>());

	//const sol::table& types{ app["types"].get<sol::table>() };
	//for (const auto& entry : types)
	//	m_typeGen.register_type(entry.first.as<std::string>(), entry.second.as<ax::type::TypeDef>());

	std::string entryPointScript = app["entry_point"];
	m_entryPoint = m_scripts.get(entryPointScript);

	if (!m_entryPoint)
	{
		spdlog::error("Failed to load entry point script: '{}'", entryPointScript);
		return false;
	}

	add_application_bindings(m_scripts.state({}));

	spdlog::info("<Lua> Running entry point script: '{}'", entryPointScript);
	const auto ep_res = m_entryPoint->run(m_env);
	if (!ep_res.valid())
	{
		const sol::error msg = ep_res;
		spdlog::error("Failed to run entry point script: {}", msg.what());
		return false;
	}

	m_loaded = true;
	return m_loaded;
}

void ax::Application::cleanup()
{
	m_scripts.cleanup({});

	if (m_window)
		m_window = nullptr;

	ax::Resource::cleanup_loader(m_loader);
}

ax::Application::~Application()
{
	if (m_loaded)
		cleanup();

	m_scripts.cleanup({});
}

ax::Application::Application(ResourceLoader&& loader)
	: m_loader{ std::move(loader) }
	, m_scripts{ {}, m_loader }
	, m_textures{ Badge<Application>{}, m_loader }
{
}
