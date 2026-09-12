#include "lua_imgui_bindings.h"

#include "spdlog/spdlog.h"
#include "imgui.h"

#include <string>
#include <tuple>
#include <cstring>

void ax::lua::bindings::setup_imgui_bindings(sol::state& state)
{
	auto ui_table{ state.create_table() };

	ui_table["w_begin"] = 
		[](const char* name) { return ImGui::Begin(name); };

	ui_table["w_end"] = // 'end' is a keyword in lua so these are now w_begin and w_end
		[]() { ImGui::End(); };

	ui_table["image"] = 
		[](const sol::table& img)
		{
			ImGui::Image((ImTextureID)(intptr_t)img["ui_view"], ImVec2(img["width"], img["height"]));
		};

	ui_table["text"] = 
		[](const char* text) { ImGui::Text("%s", text); };

	ui_table["text_wrapped"] = 
		[](const char* text) { ImGui::TextWrapped("%s", text); };

	ui_table["text_colored"] = 
		[](float r, float g, float b, float a, const char* text)
		{
			ImGui::TextColored(ImVec4(r, g, b, a), "%s", text);
		};

	ui_table["same_line"] = 
		[](float pos_x = 0.0f, float spacing_w = -1.0f) { ImGui::SameLine(pos_x, spacing_w); };

	ui_table["new_line"] = 
		[]() { ImGui::NewLine(); };

	ui_table["separator"] = 
		[]() { ImGui::Separator(); };

	ui_table["bullet_text"] = 
		[](const char* text) { ImGui::BulletText("%s", text); };

	ui_table["button"] = 
		[](const char* label) { return ImGui::Button(label); };

	ui_table["small_button"] = 
		[](const char* label) { return ImGui::SmallButton(label); };

	ui_table["checkbox"] = 
		[](const char* label, bool value) 
		{
			bool v = value;
			bool changed = ImGui::Checkbox(label, &v);
			return std::pair(v, changed);
		};

	ui_table["slider_float"] = 
		[](const char* label, float value, float min, float max) 
		{
			float v = value;
			bool changed = ImGui::SliderFloat(label, &v, min, max);
			return std::pair(v, changed);
		};

	ui_table["slider_int"] = 
		[](const char* label, int value, int min, int max) 
		{
			int v = value;
			bool changed = ImGui::SliderInt(label, &v, min, max);
			return std::pair(v, changed);
		};

	ui_table["drag_float"] = 
		[](const char* label, float value, float speed = 1.0f) 
		{
			float v = value;
			bool changed = ImGui::DragFloat(label, &v, speed);
			return std::pair(v, changed);
		};

	ui_table["input_text"] = 
		[](const char* label, const std::string& initial)
		{
			char buf[256];
			std::memset(buf, 0, sizeof(buf));
			std::memcpy(buf, initial.c_str(), sizeof(buf) - 1);
			bool changed = ImGui::InputText(label, buf, sizeof(buf));
			return std::pair(std::string(buf), changed);
		};

	ui_table["color_edit3"] = 
		[](const char* label, float r, float g, float b) 
		{
			float col[3] = { r, g, b };
			bool changed = ImGui::ColorEdit3(label, col);
			return std::make_tuple(col[0], col[1], col[2], changed);
		};

	ui_table["color_edit4"] = 
		[](const char* label, float r, float g, float b, float a) 
		{
			float col[4] = { r, g, b, a };
			bool changed = ImGui::ColorEdit4(label, col);
			return std::make_tuple(col[0], col[1], col[2], col[3], changed);
		};

	state["ui"] = ui_table;
}
