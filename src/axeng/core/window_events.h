#pragma once

#include <webgpu/webgpu_cpp.h>
#include <cstdint>
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

	// A draw into one of the lighting passes' offscreen targets (see docs/lighting.md, "Drawing into the lighting
	// passes"). The view maps world pixels to the target: viewport size, camera position and zoom.
	struct WindowLightingDrawEvent
	{
		wgpu::RenderPassEncoder& pass;
		float viewportX, viewportY;
		float cameraX, cameraY;
		float zoom;
		// Ambient occlusion only: 0 draws into the shadow casters (offset along the shadows), 1 into the silhouettes that
		// mark where casters stand.
		std::uint32_t slot;
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