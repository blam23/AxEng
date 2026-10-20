#include "axeng/core/lua/lua_engine.h"
#include "axeng/core/log_timer.h"
#include "axeng/core/lua/script.h"

#ifdef _WIN32
#include <Windows.h>
#include <TlHelp32.h>
#include <cerrno>
#include <fcntl.h>
#include <io.h>
#include <string>
#include <unordered_set>

namespace
{
	char s_popen_registry_key{};

	void push_popen_registry(lua_State* L)
	{
		lua_pushlightuserdata(L, &s_popen_registry_key);
		lua_rawget(L, LUA_REGISTRYINDEX);
		if (lua_istable(L, -1))
			return;

		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushlightuserdata(L, &s_popen_registry_key);
		lua_pushvalue(L, -2);
		lua_rawset(L, LUA_REGISTRYINDEX);
	}

	HANDLE get_popen_job(lua_State* L, int index)
	{
		if (lua_type(L, index) != LUA_TUSERDATA)
			return nullptr;

		void* userdata{ lua_touserdata(L, index) };
		if (userdata == nullptr)
			return nullptr;

		push_popen_registry(L);
		lua_pushlightuserdata(L, userdata);
		lua_rawget(L, -2);
		HANDLE job{ static_cast<HANDLE>(lua_touserdata(L, -1)) };
		lua_pop(L, 2);
		return job;
	}

	void set_popen_job(lua_State* L, int index, HANDLE job)
	{
		void* userdata{ lua_touserdata(L, index) };
		push_popen_registry(L);
		lua_pushlightuserdata(L, userdata);
		lua_pushlightuserdata(L, job);
		lua_rawset(L, -3);
		lua_pop(L, 1);
	}

	void clear_popen_job(lua_State* L, void* userdata, HANDLE job)
	{
		push_popen_registry(L);
		lua_pushlightuserdata(L, userdata);
		lua_pushnil(L);
		lua_rawset(L, -3);
		lua_pop(L, 1);
		CloseHandle(job);
	}

	std::wstring popen_shell_name()
	{
		wchar_t shell[MAX_PATH]{};
		const DWORD shellLength{ GetEnvironmentVariableW(L"COMSPEC", shell, static_cast<DWORD>(std::size(shell))) };
		std::wstring shellPath{ shellLength > 0 && shellLength < std::size(shell) ? shell : L"cmd.exe" };
		const size_t separator{ shellPath.find_last_of(L"\\/") };
		if (separator != std::wstring::npos)
			shellPath.erase(0, separator + 1);
		return shellPath;
	}

	std::unordered_set<DWORD> current_popen_shells()
	{
		std::unordered_set<DWORD> shells;
		const HANDLE snapshot{ CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
		if (snapshot == INVALID_HANDLE_VALUE)
			return shells;

		PROCESSENTRY32W entry{};
		entry.dwSize = sizeof(entry);
		if (Process32FirstW(snapshot, &entry))
		{
			const std::wstring shellName{ popen_shell_name() };
			do
			{
				if (entry.th32ParentProcessID == GetCurrentProcessId() &&
					CompareStringOrdinal(entry.szExeFile, -1, shellName.c_str(), -1, TRUE) == CSTR_EQUAL)
					shells.insert(entry.th32ProcessID);
			} while (Process32NextW(snapshot, &entry));
		}
		CloseHandle(snapshot);
		return shells;
	}

	DWORD find_new_popen_shell(const std::unordered_set<DWORD>& existingShells, HANDLE& shellProcess)
	{
		const ULONGLONG deadline{ GetTickCount64() + 1000 };
		const std::wstring shellName{ popen_shell_name() };
		do
		{
			const HANDLE snapshot{ CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
			if (snapshot != INVALID_HANDLE_VALUE)
			{
				PROCESSENTRY32W entry{};
				entry.dwSize = sizeof(entry);
				if (Process32FirstW(snapshot, &entry))
				{
					do
					{
						if (entry.th32ParentProcessID == GetCurrentProcessId() &&
							!existingShells.contains(entry.th32ProcessID) &&
							CompareStringOrdinal(entry.szExeFile, -1, shellName.c_str(), -1, TRUE) == CSTR_EQUAL)
						{
							shellProcess = OpenProcess(
								PROCESS_SET_QUOTA | PROCESS_TERMINATE | SYNCHRONIZE,
								FALSE,
								entry.th32ProcessID
							);
							if (shellProcess != nullptr)
							{
								const DWORD shellPid{ entry.th32ProcessID };
								CloseHandle(snapshot);
								return shellPid;
							}
						}
					} while (Process32NextW(snapshot, &entry));
				}
				CloseHandle(snapshot);
			}
			Sleep(5);
		} while (GetTickCount64() < deadline);

		return 0;
	}

	DWORD find_popen_child(DWORD shellPid, HANDLE shellProcess)
	{
		const ULONGLONG deadline{ GetTickCount64() + 1000 };
		do
		{
			const HANDLE snapshot{ CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
			if (snapshot != INVALID_HANDLE_VALUE)
			{
				PROCESSENTRY32W entry{};
				entry.dwSize = sizeof(entry);
				if (Process32FirstW(snapshot, &entry))
				{
					do
					{
						const bool isConsoleHost
						{
							CompareStringOrdinal(entry.szExeFile, -1, L"conhost.exe", -1, TRUE) == CSTR_EQUAL ||
							CompareStringOrdinal(entry.szExeFile, -1, L"OpenConsole.exe", -1, TRUE) == CSTR_EQUAL
						};

						if (entry.th32ParentProcessID == shellPid && !isConsoleHost)
						{
							const DWORD childPid{ entry.th32ProcessID };
							CloseHandle(snapshot);
							return childPid;
						}
					} while (Process32NextW(snapshot, &entry));
				}
				CloseHandle(snapshot);
			}

			if (WaitForSingleObject(shellProcess, 0) == WAIT_OBJECT_0)
				break;

			Sleep(5);
		} while (GetTickCount64() < deadline);

		// Failure, return 0 as the child PID to indicate that we couldn't find it.
		return 0;
	}

	void terminate_popen_processes(DWORD shellPid, HANDLE shellProcess)
	{
		const DWORD childPid{ find_popen_child(shellPid, shellProcess) };
		if (childPid != 0)
		{
			HANDLE childProcess{ OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, childPid) };
			if (childProcess != nullptr)
			{
				TerminateProcess(childProcess, 1);
				WaitForSingleObject(childProcess, INFINITE);
				CloseHandle(childProcess);
			}
		}

		TerminateProcess(shellProcess, 1);
		WaitForSingleObject(shellProcess, INFINITE);
	}

	int lua_io_popen(lua_State* L);

	int lua_popen_file_close(lua_State* L)
	{
		const int argumentCount{ lua_gettop(L) };
		void* userdata{ argumentCount > 0 && lua_type(L, 1) == LUA_TUSERDATA ? lua_touserdata(L, 1) : nullptr };
		HANDLE job{ userdata == nullptr ? nullptr : get_popen_job(L, 1) };

		lua_pushvalue(L, lua_upvalueindex(1));
		lua_insert(L, 1);
		const int status{ lua_pcall(L, argumentCount, LUA_MULTRET, 0) };
		if (job != nullptr)
			clear_popen_job(L, userdata, job);
		if (status != LUA_OK)
			return lua_error(L);
		return lua_gettop(L);
	}

	int lua_popen_file_gc(lua_State* L)
	{
		void* userdata{ lua_type(L, 1) == LUA_TUSERDATA ? lua_touserdata(L, 1) : nullptr };
		HANDLE job{ userdata == nullptr ? nullptr : get_popen_job(L, 1) };
		int status{ LUA_OK };

		if (lua_isfunction(L, lua_upvalueindex(1)))
		{
			lua_pushvalue(L, lua_upvalueindex(1));
			lua_pushvalue(L, 1);
			status = lua_pcall(L, 1, 0, 0);
		}
		if (job != nullptr)
			clear_popen_job(L, userdata, job);
		if (status != LUA_OK)
			return lua_error(L);
		return 0;
	}

	bool install_popen_close_handlers(lua_State* L)
	{
		push_popen_registry(L);
		lua_pop(L, 1);

		luaL_getmetatable(L, LUA_FILEHANDLE);
		if (!lua_istable(L, -1))
		{
			spdlog::error("Lua file handle metatable is unavailable");
			lua_pop(L, 1);
			return false;
		}

		lua_getfield(L, -1, "__index");
		if (!lua_istable(L, -1))
		{
			spdlog::error("Lua file handle methods are unavailable");
			lua_pop(L, 2);
			return false;
		}
		lua_getfield(L, -1, "close");
		if (!lua_isfunction(L, -1))
		{
			spdlog::error("Lua file handle close method is unavailable");
			lua_pop(L, 3);
			return false;
		}
		lua_pushcclosure(L, &lua_popen_file_close, 1);
		lua_setfield(L, -2, "close");
		lua_pop(L, 1);

		lua_getfield(L, -1, "__gc");
		if (!lua_isfunction(L, -1))
		{
			spdlog::error("Lua file handle finalizer is unavailable");
			lua_pop(L, 2);
			return false;
		}
		lua_pushcclosure(L, &lua_popen_file_gc, 1);
		lua_setfield(L, -2, "__gc");
		lua_pop(L, 1);

		lua_getglobal(L, "io");
		lua_getfield(L, -1, "close");
		if (!lua_isfunction(L, -1))
		{
			spdlog::error("Lua io.close function is unavailable");
			lua_pop(L, 2);
			return false;
		}
		lua_pushcclosure(L, &lua_popen_file_close, 1);
		lua_setfield(L, -2, "close");
		lua_getfield(L, -1, "popen");
		if (!lua_isfunction(L, -1))
		{
			spdlog::error("Lua io.popen function is unavailable");
			lua_pop(L, 2);
			return false;
		}
		lua_pushcclosure(L, &lua_io_popen, 1);
		lua_setfield(L, -2, "popen");
		lua_pop(L, 1);
		return true;
	}

	int lua_io_popen(lua_State* L)
	{
		luaL_checkstring(L, 1);
		const char* mode{ luaL_optstring(L, 2, "r") };
		const bool reading{ mode[0] == 'r' };
		const bool validMode{ (reading || mode[0] == 'w') &&
			(mode[1] == '\0' || ((mode[1] == 'b' || mode[1] == 't') && mode[2] == '\0')) };
		luaL_argcheck(L, validMode, 2, "invalid mode");

		const int argumentCount{ lua_gettop(L) };
		const std::unordered_set<DWORD> existingShells{ current_popen_shells() };
		lua_pushvalue(L, lua_upvalueindex(1));
		lua_pushvalue(L, 1);
		if (argumentCount > 1)
			lua_pushvalue(L, 2);
		else
			lua_pushnil(L);
		lua_call(L, 2, LUA_MULTRET);

		const int resultCount{ lua_gettop(L) - argumentCount };
		for (int i{}; i < argumentCount; ++i)
			lua_remove(L, 1);
		if (resultCount == 0 || lua_type(L, 1) != LUA_TUSERDATA)
			return resultCount;

		HANDLE shellProcess{};
		const DWORD shellPid{ find_new_popen_shell(existingShells, shellProcess) };
		DWORD childPid{};
		if (shellPid != 0)
		{
			HANDLE job{ CreateJobObjectW(nullptr, nullptr) };
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo{};
			jobInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			if (job == nullptr || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jobInfo, sizeof(jobInfo)))
			{
				const DWORD error{ GetLastError() };
				terminate_popen_processes(shellPid, shellProcess);
				CloseHandle(shellProcess);
				if (job != nullptr)
					CloseHandle(job);
				return luaL_error(L, "failed to create Lua io.popen job object: Windows error %lu", error);
			}
			if (!AssignProcessToJobObject(job, shellProcess))
			{
				const DWORD error{ GetLastError() };
				terminate_popen_processes(shellPid, shellProcess);
				CloseHandle(shellProcess);
				CloseHandle(job);
				return luaL_error(L, "failed to assign Lua io.popen shell to job object: Windows error %lu", error);
			}

			childPid = find_popen_child(shellPid, shellProcess);
			if (childPid != 0)
			{
				HANDLE childProcess{ OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, childPid) };
				if (childProcess == nullptr || !AssignProcessToJobObject(job, childProcess))
				{
					const DWORD error{ GetLastError() };
					if (childProcess != nullptr)
					{
						TerminateProcess(childProcess, 1);
						WaitForSingleObject(childProcess, INFINITE);
						CloseHandle(childProcess);
					}
					terminate_popen_processes(shellPid, shellProcess);
					CloseHandle(shellProcess);
					CloseHandle(job);
					return luaL_error(L, "failed to assign Lua io.popen child to job object: Windows error %lu", error);
				}
				CloseHandle(childProcess);
			}

			CloseHandle(shellProcess);
			set_popen_job(L, 1, job);
		}

		lua_pushinteger(L, static_cast<lua_Integer>(childPid));
		return resultCount + 1;
	}

	int lua_io_popen_denied(lua_State* L)
	{
		spdlog::error("Access to library is denied (permission not requested)");
		lua_pushnil(L);
		return 1;
	}
}
#endif

ax::lua::Manager::Manager(flag_set<Permission> requestedPermissions)
	: m_permissionFlags{ requestedPermissions }
{
}

inline void lua_panic(sol::optional<std::string> maybe_msg)
{
	if (maybe_msg)
		spdlog::error("Lua panic: {}", *maybe_msg);
	else
		spdlog::error("Lua panic: unknown error");
}

int lua_exception_handler(lua_State* L, sol::optional<const std::exception&> maybe_exception, sol::string_view description)
{
	if (maybe_exception)
		spdlog::error("Lua exception: {} ({})", description, maybe_exception->what());
	else
		spdlog::error("Lua exception: {}", description);

	return sol::stack::push(L, description);
}


ax::Error ax::lua::Manager::setup()
{
	LogTimer _timer{ "setup lua" };
	spdlog::info("LuaJIT version: {} (version number {})", LUAJIT_VERSION, LUAJIT_VERSION_NUM);

	m_state.set_panic(sol::c_call<decltype(&lua_panic), &lua_panic>);
	m_state.set_exception_handler(&lua_exception_handler);

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
	if (has_permission(Permission::flag)) m_state.require(#lib, luaopen_##lib, true); \
	else m_state[#lib] = blocked; } while(false)

	LIB_IF_PERMITTED_OR_ERROR_TABLE(IO, io);
	LIB_IF_PERMITTED_OR_ERROR_TABLE(OS, os);

#undef LIB_IF_PERMITTED_OR_ERROR_TABLE

#ifdef _WIN32
	if (has_permission(Permission::IO))
	{
		if (has_permission(Permission::OS))
		{
			if (!install_popen_close_handlers(m_state.lua_state()))
				return ax::Error::Lua;
		}
		else
		{
			m_state["io"]["popen"] = &lua_io_popen_denied;
		}
	}
#endif

	return ax::Error::Success;
}

ax::Error ax::lua::Manager::run_init()
{
	const auto initLoad{ Resource::embedded_load_as_text<Script>("@init") };
	if (!initLoad.has_value())
	{
		spdlog::error("Failed to load @init script: ResourceLoadError::{}", (int)initLoad.error());
		return ax::Error::IO;
	}

	const std::string& initScript{ initLoad.value() };
	const auto res{ m_state.do_string(initScript, "@init") };
	if (!res.valid())
	{
		sol::error err = res;
		spdlog::error("Failed to run @init script {}", err.what());
		return ax::Error::Lua;
	}

	return ax::Error::Success;
}

ax::Error ax::lua::Manager::cleanup()
{
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

ax::lua::Permission ax::lua::get_perm_from_string(const std::string& perm)
{
	if (perm == "io")
		return Permission::IO;
	else if (perm == "os")
		return Permission::OS;
	else if (perm == "file_notify")
		return Permission::FileNotify;
	else if (perm == "threads")
		return Permission::Threads;

	spdlog::error("Unknown permission string: '{}'", perm);
	return Permission::_;
}
