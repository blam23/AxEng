#include "script.h"

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
		m_code = res.get<sol::function>();
		m_loaded = true;
	}
	else
	{
		sol::error err = res;
		spdlog::error("Failed to load script: {}", err.what());
	}
}

sol::function_result ax::lua::Script::run(sol::environment& env)
{
	sol::set_environment(env, m_code);
	return m_code();
}

sol::function_result ax::lua::Script::run_no_cache(sol::environment& env)
{
	sol::function res{ m_lua.load(m_strCode, m_name) };

	if (res.valid())
	{
		sol::set_environment(env, res);
		return res();
	}

	return {};
}

std::unique_ptr<ax::lua::Script> ax::lua::ScriptManager::inner_load(const std::string& name, const Script::Descriptor& description)
{
	auto res{ m_loader.load_as_text(description) };

	if (res.has_value())
		return std::make_unique<ax::lua::Script>(Badge<ScriptManager>{}, m_lua, name, res.value());
	else
		return nullptr;
}

ax::lua::ScriptManager::ScriptManager(Badge<Module> badge, IResourceLoader& loader)
	: m_lua{ loader }
	, ax::AssetManager<Script>{ badge, loader }
{
}

sol::environment ax::lua::ScriptManager::create_env()
{
	return m_lua.create_env();
}
