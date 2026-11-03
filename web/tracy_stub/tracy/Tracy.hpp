#pragma once

// Profiling is not available on WebAssembly: these stand in for the Tracy macros the engine uses and do nothing.
#include <cstddef>

#define ZoneScoped
#define ZoneScopedN(name)
#define ZoneScopedC(color)
#define ZoneScopedNC(name, color)
#define ZoneNamedN(variable, name, active)
#define ZoneText(text, size)
#define ZoneName(text, size)
#define FrameMark
#define FrameMarkNamed(name)
#define TracyPlot(name, value) ((void)sizeof(value))
#define TracyPlotConfig(name, type, step, fill, color)
#define TracyMessage(text, size) ((void)sizeof(size))
#define TracyMessageL(text)
#define TracyIsConnected false

namespace tracy
{
	inline void SetThreadName(const char*) {}
}
