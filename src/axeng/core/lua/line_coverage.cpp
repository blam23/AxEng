#include "axeng/core/lua/line_coverage.h"
#include "axeng/core/lua/lua_engine.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define AX_GETPID _getpid
#else
#include <unistd.h>
#define AX_GETPID getpid
#endif

namespace
{
	struct StringHash
	{
		using is_transparent = void;
		size_t operator()(std::string_view text) const { return std::hash<std::string_view>{}(text); }
	};

	using LineCounts = std::unordered_map<int, uint64_t>;

	// One per Lua state: only that state's thread writes it, and it is read when the process exits.
	struct Counter
	{
		std::unordered_map<std::string, LineCounts, StringHash, std::equal_to<>> chunks;
		const std::string* lastName{ nullptr };
		LineCounts* lastChunk{ nullptr };
	};

	std::mutex s_mutex;
	std::vector<std::unique_ptr<Counter>> s_counters;
	char s_registryKey{};

	const std::string& output_pattern()
	{
		static const std::string pattern = []
		{
#ifdef _WIN32
			char* value{ nullptr };
			size_t size{ 0 };
			if (_dupenv_s(&value, &size, "AXENG_LUA_LINE_COVERAGE") != 0 || !value)
				return std::string{};
			std::string result{ value };
			std::free(value);
			return result;
#else
			const char* value = std::getenv("AXENG_LUA_LINE_COVERAGE");
			return std::string{ value ? value : "" };
#endif
		}();
		return pattern;
	}

	void write_counts()
	{
		std::string path{ output_pattern() };
		const std::string pid{ std::to_string(AX_GETPID()) };
		for (size_t at = path.find("%p"); at != std::string::npos; at = path.find("%p", at + pid.size()))
			path.replace(at, 2, pid);

		std::lock_guard lock{ s_mutex };
		std::ofstream file{ path, std::ios::binary };
		for (const auto& counter : s_counters)
			for (const auto& [chunk, lines] : counter->chunks)
				for (const auto& [line, count] : lines)
					file << chunk << '\t' << line << '\t' << count << '\n';
	}

	Counter* counter_of(lua_State* L)
	{
		thread_local lua_State* lastState{ nullptr };
		thread_local Counter* lastCounter{ nullptr };
		if (L == lastState)
			return lastCounter;

		lua_pushlightuserdata(L, &s_registryKey);
		lua_rawget(L, LUA_REGISTRYINDEX);
		auto* counter = static_cast<Counter*>(lua_touserdata(L, -1));
		lua_pop(L, 1);
		lastState = L;
		lastCounter = counter;
		return counter;
	}

	void on_line(lua_State* L, lua_Debug* ar)
	{
		Counter* counter = counter_of(L);
		if (!counter || !lua_getinfo(L, "S", ar) || !ar->source)
			return;

		// Code loaded from a string is named by the code itself; one name stands for all of it.
		std::string_view source{ ar->source };
		if (source.find_first_of("\t\n\r") != std::string_view::npos)
			source = "=[string]";

		// Consecutive lines are nearly always in the same chunk, so check that one before the lookup.
		if (!counter->lastName || *counter->lastName != source)
		{
			auto it = counter->chunks.find(source);
			if (it == counter->chunks.end())
				it = counter->chunks.emplace(std::string{ source }, LineCounts{}).first;
			counter->lastName = &it->first;
			counter->lastChunk = &it->second;
		}
		++(*counter->lastChunk)[ar->currentline];
	}
}

bool ax::lua::line_coverage::enabled()
{
	return !output_pattern().empty();
}

void ax::lua::line_coverage::attach(lua_State* L)
{
	if (!enabled())
		return;

	Counter* counter{ nullptr };
	{
		std::lock_guard lock{ s_mutex };
		static const bool registered = [] { return std::atexit(write_counts) == 0; }();
		(void)registered;
		s_counters.push_back(std::make_unique<Counter>());
		counter = s_counters.back().get();
	}

	lua_pushlightuserdata(L, &s_registryKey);
	lua_pushlightuserdata(L, counter);
	lua_rawset(L, LUA_REGISTRYINDEX);

#ifndef __EMSCRIPTEN__
	// Compiled traces do not call line hooks, so lines run in them would be missed.
	luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
#endif
	lua_sethook(L, on_line, LUA_MASKLINE, 0);
}
