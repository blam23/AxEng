#pragma once

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "axeng/core/helpers.h"
#include "axeng/core/asset_manager.h"
#include "axeng/core/lua/lua_engine.h"

namespace ax::lua
{
	class Script : public Asset
	{
	public:
		DISABLE_COPY_AND_MOVE(Script);
		using Descriptor = std::string;

	public:
		Script(Badge<ScriptManager>, ax::lua::Manager& m_lua, const std::string& name, const std::string& code);

		sol::protected_function_result run(sol::environment& env);
		sol::protected_function_result run_no_cache(sol::environment& env);
		sol::protected_function_result run_different_state(sol::state& state, sol::environment& env);
		auto& code() const { return m_strCode; };

	private:
		sol::protected_function m_code;
		sol::load_result m_res;
		std::string m_strCode;
		ax::lua::Manager& m_lua;
	};

	class ScriptManager : public AssetManager<Script, ScriptManager>
	{
	public:
		ScriptManager(Badge<Application>, flag_set<Permission> permissions, ResourceLoader& loader);
		sol::environment create_env();

		ax::Error setup(Badge<Application>);
		ax::Error cleanup(Badge<Application>);

		sol::state& state(Badge<Application>) { return m_lua.state(); }
		const sol::state& state(Badge<Application>) const { return m_lua.state(); }
		sol::state& debug_get_state(Badge<ax::debug::View>) { return m_lua.state(); }
		const sol::state& debug_get_state(Badge<ax::debug::View>) const { return m_lua.state(); }

	private:
		ax::lua::Manager m_lua;

		std::unique_ptr<Script> load_impl(const std::string& name, const Script::Descriptor& description);
		friend AssetManager<Script, ScriptManager>;
	};
}