#pragma once

#include "axeng/core/error.h"
#include "axeng/core/forward.h"
#include "axeng/core/helpers.h"
#include "axeng/core/resource_loader.h"

#include <string>
// LuaJIT where it runs; PUC Lua 5.4 on WebAssembly, which LuaJIT does not support.
#ifdef __EMSCRIPTEN__
#include <lua.hpp>
#define AX_LUA_RUNTIME_NAME "Lua"
#define AX_LUA_RUNTIME_VERSION LUA_VERSION
#define AX_LUA_RUNTIME_VERSION_NUMBER LUA_VERSION_NUM
#else
extern "C"
{
#include <luajit.h>
}

#if !defined(LUAJIT_VERSION)
#error AxEng requires LuaJIT for its sol2 Lua runtime
#endif
#define AX_LUA_RUNTIME_NAME "LuaJIT"
#define AX_LUA_RUNTIME_VERSION LUAJIT_VERSION
#define AX_LUA_RUNTIME_VERSION_NUMBER LUAJIT_VERSION_NUM
#endif

#define SOL_ALL_SAFETIES_ON 1
// Lua errors are reported through the engine's spdlog handler, not sol2's stderr logger.
#define SOL_PRINT_ERRORS 0
#include <sol/sol.hpp>
#include "axeng/external/flag_set.hpp"

namespace ax::lua
{
	enum class Permission : uint32_t
	{
		IO,
		OS,
		FileNotify,
		Threads,
		
		_
	};

	Permission get_perm_from_string(const std::string& perm);

	class Manager
	{
	public:
		DISABLE_COPY_AND_MOVE(Manager);

		explicit Manager(flag_set<Permission> requestedPermissions);
		~Manager() = default;
		 
		ax::Error setup();
		ax::Error run_init();
		ax::Error cleanup();

		sol::environment create_env();
		sol::load_result load(const std::string& code, const std::string& file);

		sol::state& state() { return m_state; }
		const sol::state& state() const { return m_state; }

		bool has_permission(Permission) const;

	private:
		sol::state m_state;
		flag_set<Permission> m_permissionFlags;
	};
}