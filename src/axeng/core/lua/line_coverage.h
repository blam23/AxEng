#pragma once

struct lua_State;

// Lua line coverage, for measuring which script lines a run executes. Off unless the environment variable
// AXENG_LUA_LINE_COVERAGE names an output file ("%p" in it becomes the process id, so several processes can share one
// pattern). When on, every Lua state the engine sets up counts the lines it runs, the JIT compiler is turned off (its
// traces skip line hooks) and the counts are written when the process exits, one "<chunk name>\t<line>\t<count>" row per
// executed line.
namespace ax::lua::line_coverage
{
	bool enabled();

	// Starts counting on this state, if coverage is on. Call once per state, before it runs any script.
	void attach(lua_State* L);
}
