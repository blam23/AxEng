#pragma once

#include <webgpu/webgpu_cpp.h>

#include <thread>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace ax
{
	// Lets WebGPU deliver pending callbacks while the caller waits for one. On the web the browser delivers them, so the
	// wait gives control back to the event loop for a moment.
	inline void poll_device(const wgpu::Device& device)
	{
#ifdef __EMSCRIPTEN__
		(void)device;
		emscripten_sleep(1);
#else
		device.Tick();
		std::this_thread::yield();
#endif
	}
}
