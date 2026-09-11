#pragma once

#include "error.h"

#include <string_view>

namespace ax::comp
{
	ax::Error clean(std::string_view outDir);
	ax::Error compile(std::string_view inDir, std::string_view outDir, bool zipItUp);
}