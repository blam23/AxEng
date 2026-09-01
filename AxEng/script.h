#pragma once

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "helpers.h"
#include "asset_manager.h"

namespace ax
{
	class Script : public Asset
	{
	public:
		DISABLE_COPY(Script);
		using Descriptor = std::string;

	public:
		Script(Badge<AssetManager<Script>>, const std::string& name, const std::string& code);

		sol::function_result run(sol::environment& env);
		sol::function_result run_no_cache(sol::environment& env);
		auto& code() const { return m_strCode; };

	private:
		sol::function m_code;
		sol::load_result m_res;
		std::string m_strCode;

	};

	using ScriptManager = AssetManager<Script>;
}