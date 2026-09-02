#pragma once

#include "helpers.h"
#include "resource_loader.h"

#include <string>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

namespace ax::lua
{
	class Manager
	{
	public:
		Manager(IResourceLoader& loader);
		sol::environment create_env();
		sol::load_result load(const std::string& code, const std::string& file);

	private:
		sol::state m_state;
		std::string m_initScript;
	};
}