#include "axeng/core/lua/script.h"

// Logging
#include "spdlog/spdlog.h"
#include "spdlog/stopwatch.h"

ax::lua::Script::Script(Badge<ScriptManager>, ax::lua::Manager& lua, const std::string& name, const std::string& code)
	: ax::Asset{ name }
	, m_strCode{ code }
	, m_lua{ lua }
{
	sol::load_result res{ m_lua.load(code, name) };

	if (res.valid())
	{
		m_code = res.get<sol::protected_function>();
		m_loaded = true;
	}
	else
	{
		sol::error err = res;
		spdlog::error("Failed to load script: {}", err.what());
	}
}

sol::protected_function_result ax::lua::Script::run(sol::environment& env)
{
	sol::set_environment(env, m_code);
	return m_code();
}

sol::protected_function_result ax::lua::Script::run_no_cache(sol::environment& env)
{
	sol::protected_function res{ m_lua.load(m_strCode, m_name) };

	if (res.valid())
	{
		sol::set_environment(env, res);
		return res();
	}

	return {};
}

sol::protected_function_result ax::lua::Script::run_different_state(sol::state& state, sol::environment& env)
{
	sol::protected_function res{ state.load(m_strCode, m_name) };

	if (res.valid())
	{
		sol::set_environment(env, res);
		return res();
	}

	return {};
}

std::unique_ptr<ax::lua::Script> ax::lua::ScriptManager::load_impl(const std::string& name, const Script::Descriptor& description)
{
	auto res
	{ 
		name.starts_with("@") 
			? Resource::embedded_load_as_text<Script>(name) 
			: Resource::load_as_text(m_loader, description)
	};

	if (res.has_value())
		return std::make_unique<ax::lua::Script>(Badge<ScriptManager>{}, m_lua, name, res.value());
	else
		return nullptr;
}

ax::lua::ScriptManager::ScriptManager(Badge<Application> badge, flag_set<Permission> permissions, ResourceLoader& loader)
	: ax::AssetManager<Script, ScriptManager>{ badge, loader }
	, m_lua{ permissions }
{
}

sol::environment ax::lua::ScriptManager::create_env()
{
	return m_lua.create_env();
}

ax::Error ax::lua::ScriptManager::setup(Badge<Application>)
{
	// Load all embedded scripts
	const auto& embedded{ Resource::get_embedded<Script>() };
	for (const auto& kvp : embedded.layout())
	{
		load(std::string{ kvp.first }, std::string{ kvp.first });
	}

	return m_lua.setup();
}

ax::Error ax::lua::ScriptManager::run_init(Badge<Application>)
{
	return m_lua.run_init();
}

ax::Error ax::lua::ScriptManager::cleanup(Badge<Application> b)
{
	unload_all(b);
	return m_lua.cleanup();
}
