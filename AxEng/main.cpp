#include "window.h"
#include "scripting.h"

int main()
{
	// TODO: parse args

	ax::Window::setup_glfw();
	{
		ax::Window window{ 1240, 720, "AxEng - Test" };
		window.init_wegbpu();
		window.run_loop();
	}
	ax::Window::teardown_glfw();
}
