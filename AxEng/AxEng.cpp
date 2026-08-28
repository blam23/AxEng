#include <iostream>
#include <chrono>

// Logging
#include "spdlog/spdlog.h"
#include "spdlog/stopwatch.h"

// Lua
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

// GFX
#include "glfw3webgpu.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <webgpu/webgpu_cpp.h>
#include <webgpu/webgpu_cpp_print.h>

int main()
{
    sol::state mainState;
	spdlog::stopwatch lua_timer;

	mainState.open_libraries
	(
		sol::lib::base,
		sol::lib::package,
		sol::lib::math,
		sol::lib::string,
		sol::lib::table,
		sol::lib::bit32
	);

	const auto res{ mainState.do_string("print(4 * 9)") };
	if (res.valid())
	{
		spdlog::info("Initialised lua, time taken: {}ms", std::chrono::duration_cast<std::chrono::nanoseconds>(lua_timer.elapsed()).count() / 1000000.);
	}
	else
	{
		sol::error err = res;
		spdlog::error("Failed to load init script: {}", err.what());
		return 1;
	}

	spdlog::stopwatch wgpu_timer;

	// Init WebGPU
	WGPUInstanceDescriptor desc{};
	desc.nextInChain = NULL;
	WGPUInstance instance = wgpuCreateInstance(&desc);

	// Init GLFW
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow* window = glfwCreateWindow(1240, 720, "AxEng", NULL, NULL);

	// Here we create our WebGPU surface from the window!
	WGPUSurface surface = glfwGetWGPUSurface(instance, window);
	printf("surface = %p", surface);

	spdlog::info("Initialised glfw window, time taken: {}ms", std::chrono::duration_cast<std::chrono::nanoseconds>(wgpu_timer.elapsed()).count() / 1000000.);

	// Terminate GLFW
	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();
	}

	glfwDestroyWindow(window);
	glfwTerminate();
}
