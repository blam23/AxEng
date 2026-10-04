#include "axeng/core/lua/bindings/lua_window_bindings.h"

#include "axeng/core/window.h"
#include "axeng/core/texture.h"

void ax::lua::bindings::setup_window_bindings(sol::state& state)
{
	auto window_table = state.create_table();

	window_table["request_close"] = 
		[](void* w) -> void
		{
			glfwSetWindowShouldClose((GLFWwindow*)w, true);
		};

	window_table["set_title"] =
		[](void* w, const std::string& str) -> void
		{
			glfwSetWindowTitle((GLFWwindow*)w, str.c_str());
		};

	state["window"] = window_table;
}
