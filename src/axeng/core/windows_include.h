#pragma once

// <windows.h> without the macros that collide with ordinary names: min and max (NOMINMAX, which the build sets) and
// near and far (16-bit pointer qualifiers, defined empty). Include this instead of <windows.h>.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#undef near
#undef far
// Windows headers included later spell them NEAR and FAR, which expanded to near and far: keep those empty.
#undef NEAR
#define NEAR
#undef FAR
#define FAR
#endif
