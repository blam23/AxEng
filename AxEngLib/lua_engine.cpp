#include "lua_engine.h"
#include "lua_bindings.h"
#include "log_timer.h"
#include "script.h"

ax::lua::Manager::Manager(flag_set<Permission> requestedPermissions)
	: m_permissionFlags{ requestedPermissions }
{
}

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

	// Always open these libraries - open the rest depending on permissions later.
	m_state.open_libraries
	(
		sol::lib::base,
		sol::lib::package,
		sol::lib::math,
		sol::lib::string,
		sol::lib::table,
		sol::lib::bit32
	);

	sol::table blocked{ m_state.create_table() };
	sol::table mt{ m_state.create_table() };
	mt[sol::meta_function::index] = 
		[](sol::table, sol::object) -> sol::object
		{
			spdlog::error("Access to library is denied (permission not requested)");
			return sol::lua_nil;
		};
	mt[sol::meta_function::new_index] = 
		[](sol::table, sol::object, sol::object)
		{
			spdlog::error("Access to library is denied (permission not requested)");
		};
	blocked[sol::metatable_key] = mt;

#define LIB_IF_PERMITTED_OR_ERROR_TABLE(flag, lib) do { \
	if (has_permission(Permission::##flag)) m_state.require(#lib, luaopen_##lib, true); \
	else m_state[#lib] = blocked; } while(false)

	LIB_IF_PERMITTED_OR_ERROR_TABLE(IO, io);
	LIB_IF_PERMITTED_OR_ERROR_TABLE(OS, os);

#undef LIB_IF_PERMITTED_OR_ERROR_TABLE

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

bool ax::lua::Manager::has_permission(ax::lua::Permission flag) const
{
	return m_permissionFlags[flag];
}