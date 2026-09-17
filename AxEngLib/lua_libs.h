#pragma once

#include <string>
#include "resource_loader.h"

namespace ax::lua::libs
{
	ax::EmbeddedResource get(const std::string& name);
	void load_all_embedded();
}