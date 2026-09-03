#pragma once

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "helpers.h"
#include "asset_manager.h"
#include "lua_engine.h"

namespace ax::lua
{
	class Script : public Asset
	{
	public:
		DISABLE_COPY(Script);
		using Descriptor = std::string;

	public:
		Script(Badge<ScriptManager>,ax::lua::Manager& m_lua, const std::string& name, const std::string& code);

		sol::function_result run(sol::environment& env);
		sol::function_result run_no_cache(sol::environment& env);
		auto& code() const { return m_strCode; };

	private:
		sol::function m_code;
		sol::load_result m_res;
		std::string m_strCode;
		ax::lua::Manager& m_lua;
	};

	class ScriptManager : public AssetManager<Script>
	{
	public:
		ScriptManager(Badge<Module>, IResourceLoader& loader);

		virtual std::unique_ptr<Script> inner_load(const std::string& name, const Script::Descriptor& description) override;

		sol::environment create_env();

	private:
		ax::lua::Manager m_lua;
	};
}