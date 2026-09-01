#include "lua_engine.h"
#include "lua_bindings.h"
#include "log_timer.h"

thread_local sol::state mainState;
thread_local bool attemptedSetup;

std::string init_script;

void ax::lua::global_setup(IResourceLoader& loader)
{
	const auto& raw_data{ loader.load("lua/init.lua") };
	init_script = { raw_data.begin(), raw_data.end() };
}

void ax::lua::init_current_thread()
{
	LogTimer _timer{ "init lua on thread" };

	mainState.open_libraries
	(
		sol::lib::base,
		sol::lib::package,
		sol::lib::math,
		sol::lib::string,
		sol::lib::table,
		sol::lib::bit32
	);

	bindings::setup_all(mainState);

	const auto res{ mainState.do_string(init_script, "Init Script")};
	if (!res.valid())
	{
		sol::error err = res;
		spdlog::error("Failed to load init script {}", err.what());
	}

	attemptedSetup = true;
}

void ax::lua::teardown_current_thread()
{
	mainState = nullptr;
	attemptedSetup = false;
}

sol::environment ax::lua::create_env()
{
	if (!attemptedSetup)
		init_current_thread();

	return { mainState, sol::create, mainState.globals() };
}

sol::load_result ax::lua::load(const std::string& code, const std::string& file)
{
	if (!attemptedSetup)
		init_current_thread();

	return mainState.load(code, file);
}
