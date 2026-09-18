#pragma once

#include "imgui.h"
#include <string>

namespace ax
{
	class ImGuiHelper
	{
	public:
		static void add_font(std::string_view name, ImFont* font);
		static ImFont* get(const std::string& name);
	};
}