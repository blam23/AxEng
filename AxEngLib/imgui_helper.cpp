#include "imgui_helper.h"

#include <map>
#include <mutex>

std::map<std::string, ImFont*> fonts;
std::mutex fontMutex;

void ax::ImGuiHelper::add_font(std::string_view name, ImFont* font)
{
	std::lock_guard lock{ fontMutex };

	fonts.emplace(name, font);
}

ImFont* ax::ImGuiHelper::get_font(const std::string& name)
{
	std::lock_guard lock{ fontMutex };

	const auto itr{ fonts.find(name) };
	if (itr == fonts.end())
		return nullptr;

	return itr->second;
}
