#include "window.h"
#include "scripting.h"

// Logging
#include "spdlog/spdlog.h"
#include "spdlog/stopwatch.h"

int main()
{
	// TODO: parse args

#ifdef _DEBUG
	spdlog::set_level(spdlog::level::debug);
#endif

	ax::setup_glfw();
	{
		ax::Script script;
		auto& state{ script.get_state() };
		auto res{ state.do_string("return 1 + 4") };

		spdlog::debug("Result: {}", res.get<int>());

		ax::Window window{ 1920, 1080, "AxEng - Test" };
		window.init_wegbpu();
		window.run_loop();
	}
	ax::teardown_glfw();
}
