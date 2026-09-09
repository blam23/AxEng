#include "axeng.h"

void ax::init()
{
	ax::setup_glfw();
	ax::lua::bind_all();
}

void ax::teardown()
{
	ax::teardown_glfw();
}