#include "axeng/core/lua/lua_engine.h"
#include "axeng/core/lua/lua_bindings.h"
#include "axeng/core/log_timer.h"
#include "axeng/core/lua/script.h"

#ifdef _WIN32
#include <Windows.h>
#include <TlHelp32.h>
#include <cerrno>
#include <fcntl.h>
#include <io.h>
#include <string>

namespace
{
	struct PopenStream
	{
		luaL_Stream stream{};
		HANDLE process{ nullptr };
		HANDLE job{ nullptr };
	};

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

	int lua_popen_close(lua_State* L)
	{
		auto* popenStream{ static_cast<PopenStream*>(luaL_checkudata(L, 1, LUA_FILEHANDLE)) };
		luaL_Stream* stream{ &popenStream->stream };
		std::fclose(stream->f);
		stream->f = nullptr;

		int status{ -1 };
		if (popenStream->process != nullptr)
		{
			DWORD exitCode{};
			if (WaitForSingleObject(popenStream->process, INFINITE) == WAIT_OBJECT_0 &&
				GetExitCodeProcess(popenStream->process, &exitCode))
				status = static_cast<int>(exitCode);
			CloseHandle(popenStream->process);
			popenStream->process = nullptr;
		}
		if (popenStream->job != nullptr)
		{
			CloseHandle(popenStream->job);
			popenStream->job = nullptr;
		}

		return luaL_execresult(L, status);
	}

	int lua_io_popen(lua_State* L)
	{
		const char* command{ luaL_checkstring(L, 1) };
		const char* mode{ luaL_optstring(L, 2, "r") };
		const bool reading{ mode[0] == 'r' };
		const bool validMode{ (reading || mode[0] == 'w') &&
			(mode[1] == '\0' || ((mode[1] == 'b' || mode[1] == 't') && mode[2] == '\0')) };
		luaL_argcheck(L, validMode, 2, "invalid mode");

		const int commandLength{ MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, command, -1, nullptr, 0) };
		if (commandLength == 0)
			return luaL_argerror(L, 1, "command must be valid UTF-8");
		std::wstring wideCommand(static_cast<size_t>(commandLength), L'\0');
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, command, -1, wideCommand.data(), commandLength);
		wideCommand.pop_back();

		SECURITY_ATTRIBUTES securityAttributes{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
		HANDLE pipeRead{};
		HANDLE pipeWrite{};
		if (!CreatePipe(&pipeRead, &pipeWrite, &securityAttributes, 0))
		{
			errno = EIO;
			return luaL_fileresult(L, 0, command);
		}

		HANDLE parentPipe{ reading ? pipeRead : pipeWrite };
		HANDLE childPipe{ reading ? pipeWrite : pipeRead };
		if (!SetHandleInformation(parentPipe, HANDLE_FLAG_INHERIT, 0))
		{
			CloseHandle(pipeRead);
			CloseHandle(pipeWrite);
			errno = EIO;
			return luaL_fileresult(L, 0, command);
		}

		const int openFlags{ (reading ? _O_RDONLY : _O_WRONLY) | (mode[1] == 'b' ? _O_BINARY : _O_TEXT) };
		const int fd{ _open_osfhandle(reinterpret_cast<intptr_t>(parentPipe), openFlags) };
		if (fd == -1)
		{
			CloseHandle(parentPipe);
			CloseHandle(childPipe);
			errno = EIO;
			return luaL_fileresult(L, 0, command);
		}
		FILE* file{ _fdopen(fd, mode) };
		if (file == nullptr)
		{
			_close(fd);
			CloseHandle(childPipe);
			errno = EIO;
			return luaL_fileresult(L, 0, command);
		}

		wchar_t shell[MAX_PATH]{};
		const DWORD shellLength{ GetEnvironmentVariableW(L"COMSPEC", shell, static_cast<DWORD>(std::size(shell))) };
		const std::wstring shellPath{ shellLength > 0 && shellLength < std::size(shell) ? shell : L"cmd.exe" };
		std::wstring commandLine{ L"\"" + shellPath + L"\" /d /c " + wideCommand };

		STARTUPINFOW startupInfo{};
		startupInfo.cb = sizeof(startupInfo);
		startupInfo.dwFlags = STARTF_USESTDHANDLES;
		startupInfo.hStdInput = reading ? GetStdHandle(STD_INPUT_HANDLE) : childPipe;
		startupInfo.hStdOutput = reading ? childPipe : GetStdHandle(STD_OUTPUT_HANDLE);
		startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
		HANDLE job{ CreateJobObjectW(nullptr, nullptr) };
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo{};
		jobInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		if (job == nullptr || !SetInformationJobObject(job, JobObjectExtendedLimitInformation,
			&jobInfo, sizeof(jobInfo)))
		{
			if (job != nullptr)
				CloseHandle(job);
			std::fclose(file);
			CloseHandle(childPipe);
			errno = EIO;
			return luaL_fileresult(L, 0, command);
		}

		PROCESS_INFORMATION processInfo{};
		if (!CreateProcessW(shellPath.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
			CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startupInfo, &processInfo))
		{
			CloseHandle(job);
			std::fclose(file);
			CloseHandle(childPipe);
			errno = EIO;
			return luaL_fileresult(L, 0, command);
		}
		if (!AssignProcessToJobObject(job, processInfo.hProcess) || ResumeThread(processInfo.hThread) == static_cast<DWORD>(-1))
		{
			TerminateProcess(processInfo.hProcess, 1);
			WaitForSingleObject(processInfo.hProcess, INFINITE);
			CloseHandle(processInfo.hThread);
			CloseHandle(processInfo.hProcess);
			CloseHandle(job);
			std::fclose(file);
			CloseHandle(childPipe);
			errno = EIO;
			return luaL_fileresult(L, 0, command);
		}
		CloseHandle(childPipe);
		CloseHandle(processInfo.hThread);
		const DWORD popenPid{ find_popen_child(processInfo.dwProcessId, processInfo.hProcess) };

		auto* popenStream{ static_cast<PopenStream*>(lua_newuserdatauv(L, sizeof(PopenStream), 0)) };
		popenStream->stream.f = file;
		popenStream->stream.closef = &lua_popen_close;
		popenStream->process = processInfo.hProcess;
		popenStream->job = job;
		luaL_setmetatable(L, LUA_FILEHANDLE);
		lua_pushinteger(L, static_cast<lua_Integer>(popenPid));

		return 2;
	}
}
#endif

ax::lua::Manager::Manager(Application& application, flag_set<Permission> requestedPermissions)
	: m_permissionFlags{ requestedPermissions }
	, m_application{ &application }
{
}

ax::lua::Manager::Manager(flag_set<Permission> requestedPermissions)
	: m_permissionFlags{ requestedPermissions }
{
}

ax::lua::Manager::~Manager()
{
	if (m_loaded)
	{
		if (m_application)
			bindings::cleanup_state(*m_application, m_state);
		else
			bindings::cleanup_state(m_state);
	}
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

	const auto initLoad{ Resource::embedded_load_as_text<Script>("@init") };
	std::string initScript{};
	if (initLoad.has_value())
	{
		initScript = initLoad.value();
	}
	else
	{
		spdlog::error("Failed to load @init script: ResourceLoadError::{}", (int)initLoad.error());
		return ax::Error::IO;
	}

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
	if (has_permission(Permission::IO) && has_permission(Permission::OS))
		m_state["io"]["popen"] = &lua_io_popen;
#endif

	bindings::bind_to_state(m_state);

	const auto res{ m_state.do_string(initScript, "@init") };
	if (!res.valid())
	{
		sol::error err = res;
		spdlog::error("Failed to run @init script {}", err.what());
		return ax::Error::Lua;
	}

	m_loaded = true;
	return ax::Error::Success;
}

ax::Error ax::lua::Manager::cleanup()
{
	m_loaded = false;
	if (m_application)
		bindings::cleanup_state(*m_application, m_state);
	else
		bindings::cleanup_state(m_state);

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
