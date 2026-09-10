#pragma once

#include <spdlog/spdlog.h>
#include <map>
#include <string>
#include <string_view>

#include "error_def.h"

namespace ax
{
#define ERR(n, i) n = i,
	enum class Error
	{
		ERRORS
	};
#undef ERR

	std::string_view get_error_name(Error e);

#define AX_RETURN_ERROR_IF_FAIL(ret, x) { ret = x; if(ret != ax::Error::Success) { spdlog::error("Operation failed: {}", get_error_name(ret)); return ret; } }

}

#undef ERRORS
