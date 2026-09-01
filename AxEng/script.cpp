#include "script.h"
#include "lua_engine.h"

// Logging
#include "spdlog/spdlog.h"
#include "spdlog/stopwatch.h"

ax::Script::Script(Badge<ScriptManager>, const std::string& name, const std::string& code)
	: ax::Asset{ name }
	, m_strCode{ code }
{
	sol::load_result res{ lua::load(code, name) };

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

sol::function_result ax::Script::run(sol::environment& env)
{
	sol::set_environment(env, m_code);
	return m_code();
}

sol::function_result ax::Script::run_no_cache(sol::environment& env)
{
	sol::function res{ lua::load(m_strCode, m_name) };

	if (res.valid())
	{
		sol::set_environment(env, res);
		return res();
	}

	return {};
}

template <>
std::unique_ptr<ax::Script> ax::ScriptManager::inner_load(IResourceLoader& loader, const std::string& name, const Script::Descriptor& description)
{
	const auto& raw_data{ loader.load(description) };
	const std::string init_script { raw_data.begin(), raw_data.end() };

	return std::make_unique<ax::Script>(Badge<ScriptManager>{}, name, init_script);
}