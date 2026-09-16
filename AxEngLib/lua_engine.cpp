#include "lua_engine.h"
#include "lua_bindings.h"
#include "log_timer.h"
#include "script.h"

ax::lua::Manager::~Manager()
{
	if (m_loaded)
		bindings::cleanup_state(m_state);
}

ax::Error ax::lua::Manager::setup()
{
	LogTimer _timer{ "setup lua" };

	const auto initLoad{ Resource::embedded_load_as_text<Script>("@init") };
	std::string initScript{};
	if (initLoad.has_value())
	{
		initScript = initLoad.value();
	}
	else
	{
		spdlog::error("Failed to load @init script: ResourceLoadError::{}", (int)initLoad.error());
		return ax::Error::IO;
	}

	m_state.open_libraries
	(
		sol::lib::base,
		sol::lib::package,
		sol::lib::math,
		sol::lib::string,
		sol::lib::table,
		sol::lib::bit32,
		sol::lib::io
	);

	bindings::bind_to_state(m_state);

	const auto res{ m_state.do_string(initScript, "@init") };
	if (!res.valid())
	{
		sol::error err = res;
		spdlog::error("Failed to run @init script {}", err.what());
		return ax::Error::Lua;
	}

	m_loaded = true;
	return ax::Error::Success;
}

ax::Error ax::lua::Manager::cleanup()
{
	m_loaded = false;
	bindings::cleanup_state(m_state);

	return ax::Error::Success;
}

sol::environment ax::lua::Manager::create_env()
{
	return { m_state, sol::create, m_state.globals() };
}

sol::load_result ax::lua::Manager::load(const std::string& code, const std::string& file)
{
	return m_state.load(code, file, sol::load_mode::any);
}
