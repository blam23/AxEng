#include "lua_imgui_bindings.h"
#include "imgui_helper.h"

#include "spdlog/spdlog.h"
#include "imgui.h"
#include "ImGuiNotify.hpp"

#include <string>
#include <tuple>
#include <cstring>

void ax::lua::bindings::setup_imgui_bindings(sol::state& state)
{
	auto ui_table{ state.create_table() };

	ui_table["begin_window"] = 
		[](const char* name) { return ImGui::Begin(name); };

	ui_table["end_window"] = // 'end' is a keyword in lua so these are now postfix'd with _window
		[]() { ImGui::End(); };

	ui_table["image"] = 
		[](const sol::table& img)
		{
			ImGui::Image((ImTextureID)(intptr_t)img["ui_view"], ImVec2(img["width"], img["height"]));
		};

	ui_table["text"] = 
		[](const char* text) { ImGui::Text("%s", text); };

	ui_table["text_wrap"] = 
		[](const char* text) { ImGui::TextWrapped("%s", text); };

	ui_table["text_color"] = 
		[](float r, float g, float b, float a, const char* text)
		{
			ImGui::TextColored(ImVec4(r, g, b, a), "%s", text);
		};

	ui_table["dock_space_over_viewport"] =
		[]() { ImGui::DockSpaceOverViewport(); };

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

	ui_table["begin_main_menu_bar"] =
		[]() { return ImGui::BeginMainMenuBar(); };

	ui_table["end_main_menu_bar"] =
		[]() { return ImGui::EndMainMenuBar(); };

	ui_table["begin_menu_bar"] =
		[]() { return ImGui::BeginMenuBar(); };

	ui_table["end_menu_bar"] =
		[]() { return ImGui::EndMenuBar(); };

	ui_table["same_line"] =
		[]() { ImGui::SameLine(); };

	ui_table["get_window_size"] =
		[]() { return std::pair(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y); };

	ui_table["same_line_offset"] =
		[](float offset_x) { ImGui::SameLine(offset_x); };

	ui_table["push_item_width"] =
		[](float width) { ImGui::PushItemWidth(width); };

	ui_table["set_next_item_width"] =
		[](float width) { ImGui::SetNextItemWidth(width); };
	
	ui_table["pop_item_width"] =
		[]() { ImGui::PopItemWidth(); };

	ui_table["begin_listbox"] =
		[](const char* label) { return ImGui::BeginListBox(label); };

	ui_table["begin_sized_listbox"] =
		[](const char* label, float w, float h) { return ImGui::BeginListBox(label, ImVec2(w, h)); };

	ui_table["end_listbox"] =
		[]() { ImGui::EndListBox(); };

	ui_table["selectable"] =
		[](const char* label, bool selected = false)
		{
			bool v = selected;
			bool changed = ImGui::Selectable(label, &v);
			return std::pair(v, changed);
		};

	ui_table["input_text"] =
		[](const char* label, const std::string& initial)
		{
			char buf[256];
			std::memset(buf, 0, sizeof(buf));
			std::memcpy(buf, initial.c_str(), initial.size());
			bool changed = ImGui::InputText(label, buf, sizeof(buf));
			return std::pair(std::string(buf), changed);
		};

	auto notification_table{ state.create_table() };
	notification_table["None"] = ImGuiToastType::None;
	notification_table["Success"] = ImGuiToastType::Success;
	notification_table["Warning"] = ImGuiToastType::Warning;
	notification_table["Error"] = ImGuiToastType::Error;
	notification_table["Info"] = ImGuiToastType::Info;
	ui_table["toast_type"] = notification_table;

	ui_table["insert_toast"] =
		[](ImGuiToastType type, int duration, const char* message)
		{
			ImGui::InsertNotification({ type, duration, message });
		};

	ui_table["begin_popup_modal"] =
		[](const char* name) 
		{ 
			return ImGui::BeginPopupModal(name);
		};

	ui_table["end_popup"] =
		[]() { ImGui::EndPopup(); };

	ui_table["close_current_popup"] =
		[]() { ImGui::CloseCurrentPopup(); };

	ui_table["open_popup"] =
		[](const char* name) { ImGui::OpenPopup(name); };

	ui_table["begin_combo"] =
		[](const char* label, const char* preview_value) { return ImGui::BeginCombo(label, preview_value); };

	ui_table["end_combo"] =
		[]() { ImGui::EndCombo(); };

	ui_table["push_font"] =
		sol::overload
		(
			[](const std::string& name, float size) { ImGui::PushFont(ax::ImGuiHelper::get(name), size); },
			[](const std::string& name) { ImGui::PushFont(ax::ImGuiHelper::get(name), 0.0f); }
		);

	ui_table["pop_font"] =
		[]() { ImGui::PopFont(); };

	ui_table["separator_text"] =
		[](const char* text) { ImGui::SeparatorText(text); };

	ui_table["separator"] =
		[]() { ImGui::Separator(); };

	state["ui"] = ui_table;
}
