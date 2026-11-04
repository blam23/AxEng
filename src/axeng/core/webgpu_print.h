#pragma once

// Dawn installs the printing helpers beside the C++ bindings; the Emscripten port keeps them under dawn/.
#if __has_include(<webgpu/webgpu_cpp_print.h>)
#include <webgpu/webgpu_cpp_print.h>
#else
#include <dawn/webgpu_cpp_print.h>
#endif
