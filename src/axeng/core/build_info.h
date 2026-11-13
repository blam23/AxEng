#pragma once

#include <string_view>

namespace ax::build
{
#if defined(AX_DEBUG_BUILD)
	inline constexpr bool is_debug = true;
	inline constexpr std::string_view mode = "debug";
#else
	inline constexpr bool is_debug = false;
	inline constexpr std::string_view mode = "release";
#endif

	// The platform the engine was built for: "web" (WebAssembly in a browser), "windows", "linux" or "macos".
#if defined(__EMSCRIPTEN__)
	inline constexpr std::string_view platform = "web";
#elif defined(_WIN32)
	inline constexpr std::string_view platform = "windows";
#elif defined(__APPLE__)
	inline constexpr std::string_view platform = "macos";
#elif defined(__linux__)
	inline constexpr std::string_view platform = "linux";
#else
	inline constexpr std::string_view platform = "unknown";
#endif
}
