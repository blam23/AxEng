#pragma once

#include "helpers.h"
#include "resource_loader.h"

#include <string>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

namespace ax::lua
{
	void global_setup(IResourceLoader& loader);
	void init_current_thread();
	void teardown_current_thread();
	sol::environment create_env();
	sol::load_result load(const std::string& code, const std::string& file);
}