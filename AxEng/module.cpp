#include "module.h"

ax::Module ax::Module::from_directory(std::string_view root)
{
	return { std::make_unique<DirectoryResourceLoader>(root) };
}

ax::Module::~Module()
{
	m_scripts.unload_all({});
}

bool ax::Module::init_window(const sol::environment& env)
{
	LogTimer _timer{ "wgpu initial setup" };

	const auto& app{ env["app"] };
	const auto& window{ app["window"] };
	m_window = std::make_unique<Window>(WindowDefinition
	{
		.width = window["width"],
		.height = window["height"],
		.title = window["title"],
		.vsync = window["v-sync"],
	});

	bool success{ true };
	success = m_window->init_webgpu();

	if (!success)
		return false;

	success = m_window->init_imgui();

	return success;
}

void ax::Module::add_manifest_bindings(sol::environment&)
{
}

bool ax::Module::try_load()
{
	LogTimer _timer{ "Module Load" };

	ax::lua::Script* manifest{ m_scripts.load("!manifest", "manifest.lua") };
	auto env{ m_scripts.create_env() };

	add_manifest_bindings(env);

	if (!manifest)
	{
		spdlog::error("Failed to load manifest.lua");
		return false;
	}

	const auto& res{ manifest->run(env) };

	if (!res.valid())
	{
		const sol::error msg = res;
		spdlog::error("Failed to parse manifest.lua: {}", msg.what());
		return false;
	}

	const auto& app{ env["app"] };
	m_name = app["name"];

	std::string entryPointScript = app["entry_point"];
	m_entryPoint = m_scripts.load("!entry", entryPointScript);

	if (!m_entryPoint)
	{
		spdlog::error("Failed to load entry point script: '{}'", entryPointScript);
		return false;
	}

	init_window(env);

	const sol::table& textures{ app["textures"].get<sol::table>() };
	for (const auto& entry : textures)
	{
		m_textures.load(entry.first.as<std::string>(), 
			{
				.path = entry.second.as<std::string>(),
				.device = m_window->device()
			});
	}

	m_loaded = true;
	return m_loaded;
}

ax::Module::Module(std::unique_ptr<IResourceLoader> loader)
	: m_loader{ std::move(loader) }
	, m_scripts{ {}, *m_loader }
	, m_textures{ Badge<Module>{}, *m_loader }
{
}
