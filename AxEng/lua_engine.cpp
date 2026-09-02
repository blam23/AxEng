#include "lua_engine.h"
#include "lua_bindings.h"
#include "log_timer.h"

ax::lua::Manager::Manager(IResourceLoader& loader)
	: m_initScript{}
{
	LogTimer _timer{ "init lua" };

	const auto initLoad{ loader.load_as_text("lua/init.lua") };
	if (initLoad.has_value())
	{
		m_initScript = initLoad.value();
	}
	else
	{
		spdlog::error("Failed to load init script: ResourceLoadError::{}", (int)initLoad.error());
		return;
	}

	m_state.open_libraries
	(
		sol::lib::base,
		sol::lib::package,
		sol::lib::math,
		sol::lib::string,
		sol::lib::table,
		sol::lib::bit32
	);

	bindings::setup_all(m_state);

	const auto res{ m_state.do_string(m_initScript, "Init Script") };
	if (!res.valid())
	{
		sol::error err = res;
		spdlog::error("Failed to run init script {}", err.what());
	}
}

sol::environment ax::lua::Manager::create_env()
{
	return { m_state, sol::create, m_state.globals() };
}

sol::load_result ax::lua::Manager::load(const std::string& code, const std::string& file)
{
	return m_state.load(code, file);
}
