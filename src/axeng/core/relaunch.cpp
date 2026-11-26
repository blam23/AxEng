#include "axeng/core/relaunch.h"

#include "spdlog/spdlog.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <spawn.h>
#endif

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
extern char** environ;
#endif

namespace ax
{
	bool relaunch(const std::vector<std::pair<std::string, std::string>>& environment)
	{
#if defined(__EMSCRIPTEN__)
		// As "NAME=value" lines, for the page to pass on as it sees fit.
		std::string lines{};
		for (const auto& [name, value] : environment) lines += name + "=" + value + "\n";
		return MAIN_THREAD_EM_ASM_INT({
			if (!globalThis.axRelaunch) return 0;
			globalThis.axRelaunch(UTF8ToString($0));
			return 1;
		}, lines.c_str()) != 0;
#elif defined(_WIN32)
		for (const auto& [name, value] : environment)
			if (!SetEnvironmentVariableA(name.c_str(), value.c_str())) return false;
		std::wstring commandLine{ GetCommandLineW() };
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
		{
			spdlog::error("Couldn't start the application again (error {})", GetLastError());
			return false;
		}
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		return true;
#elif defined(__linux__)
		for (const auto& [name, value] : environment)
			if (setenv(name.c_str(), value.c_str(), 1) != 0) return false;
		std::ifstream file{ "/proc/self/cmdline", std::ios::binary };
		const std::string text{ std::istreambuf_iterator<char>{ file }, {} };
		std::vector<std::string> arguments{};
		for (std::size_t start{}; start < text.size();)
		{
			const auto end{ text.find('\0', start) };
			arguments.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
			if (end == std::string::npos) break;
			start = end + 1;
		}
		std::vector<char*> argv{};
		for (auto& argument : arguments) argv.push_back(argument.data());
		argv.push_back(nullptr);
		pid_t child{};
		if (posix_spawn(&child, "/proc/self/exe", nullptr, nullptr, argv.data(), environ) != 0)
		{
			spdlog::error("Couldn't start the application again");
			return false;
		}
		return true;
#else
		(void)environment;
		return false;
#endif
	}
}
