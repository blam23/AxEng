#pragma once

#include <lua.hpp>

namespace tracy
{
	// The scripts call tracy.ZoneBeginN, tracy.ZoneEnd and tracy.Message; without a profiler they do nothing.
	inline void LuaRegister(lua_State* L)
	{
		lua_newtable(L);
		for (const char* name : { "ZoneBegin", "ZoneBeginN", "ZoneBeginS", "ZoneBeginNS", "ZoneEnd", "ZoneText", "ZoneName", "Message" })
		{
			lua_pushcfunction(L, [](lua_State*) -> int { return 0; });
			lua_setfield(L, -2, name);
		}
		lua_setglobal(L, "tracy");
	}
}
