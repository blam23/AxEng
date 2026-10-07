#pragma once

#include <webgpu/webgpu_cpp.h>
#include "axeng/core/forward.h"

namespace ax
{
	struct WindowUpdateEvent
	{
		double delta;
	};

	struct WindowRequestCloseEvent
	{
		Window* window;
	};

	// Before the render pass is setup
	struct WindowPreRenderEvent
	{
		double delta;
	};

	// After the render pass is setup
	struct WindowRenderEvent
	{
		double delta;
		wgpu::RenderPassEncoder& pass;
	};

	struct WindowUIEvent
	{
		double delta;
	};

	struct WindowResizeEvent
	{
		uint32_t width;
		uint32_t height;
	};
}