#include "lua_libs.h"

#include "windows.h"
#include "resource.h"
#include <numbers>
#include <map>

std::map<std::string, ax::EmbeddedResource> s_embedded{};

void load(std::string&& name, int resourceID)
{
	auto resource{ ::FindResource(nullptr, MAKEINTRESOURCE(resourceID), RT_RCDATA)};
	auto memory{ ::LoadResource(nullptr, resource) };

	std::size_t resourceSize{ ::SizeofResource(nullptr, resource) };
	void* ptr{ ::LockResource(memory) };

	s_embedded.emplace(name, ax::EmbeddedResource{ (const uint8_t*)ptr, resourceSize });
}

ax::EmbeddedResource ax::lua::libs::get(const std::string& name)
{
	return s_embedded[name];
}

void ax::lua::libs::load_all_embedded()
{
	load("@init", IDR_INIT_LUA);
	load("@std", IDR_STD_LUA);
	load("@json", IDR_JSON_LUA);
	load("@compiler_core", IDR_COMPILER_CORE_LUA);
}
