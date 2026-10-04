#include "axeng/core/lua/external/lua_libs.h"

#include <map>

namespace
{
	// #embed paths are resolved relative to this file
	constexpr unsigned char s_init[] = {
#embed "init.lua"
	};
	constexpr unsigned char s_std[] = {
#embed "std.lua"
	};
	constexpr unsigned char s_json[] = {
#embed "json.lua"
	};
	constexpr unsigned char s_compiler_core[] = {
#embed "compiler_core.lua"
	};

	std::map<std::string, ax::EmbeddedResource> s_embedded{};

	template<std::size_t N>
	void load(std::string&& name, const unsigned char (&data)[N])
	{
		s_embedded.emplace(std::move(name), ax::EmbeddedResource{ data, N });
	}
}

ax::EmbeddedResource ax::lua::libs::get(const std::string& name)
{
	return s_embedded[name];
}

void ax::lua::libs::load_all_embedded()
{
	load("@init", s_init);
	load("@std", s_std);
	load("@json", s_json);
	load("@compiler_core", s_compiler_core);
}
