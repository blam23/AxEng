#include "lua_window_bindings.h"

#include "window.h"
#include "texture.h"

void ax::lua::bindings::setup_window_bindings(sol::state& env)
{
	auto window_table = env.create_table();

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

	env["window"] = window_table;
}
