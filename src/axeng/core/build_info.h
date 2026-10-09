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
}
