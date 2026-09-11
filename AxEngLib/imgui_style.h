#pragma once

#include "imgui.h"

namespace ax
{
	void setup_imgui_style()
	{
		ImGuiStyle& style = ImGui::GetStyle();
		style.Alpha = 1.0f;
		style.WindowMenuButtonPosition = ImGuiDir_None;
		style.ColorButtonPosition = ImGuiDir_Left;
		style.WindowPadding = ImVec2(8.0f, 8.0f);
		style.FramePadding = ImVec2(16.0f, 5.0f);
		style.ItemSpacing = ImVec2(8.0f, 8.0f);
		style.ItemInnerSpacing = ImVec2(1.0f, 1.0f);
		style.SeparatorTextAlign = ImVec2(0.5f, 0.5f);
		style.SeparatorTextBorderSize = 1;
		style.TabRounding = 0.0f;
		style.ChildRounding = 0.0f;
		style.WindowRounding = 0.0f;
		style.IndentSpacing = 9.0f;
		style.GrabMinSize = 20.0f;

		style.DisabledAlpha = 0.6000000238418579f;
		style.WindowRounding = 0.0f;
		style.WindowBorderSize = 1.0f;
		style.WindowMinSize = ImVec2(32.0f, 32.0f);
		style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
		style.ChildRounding = 0.0f;
		style.ChildBorderSize = 1.0f;
		style.PopupRounding = 2.200000047683716f;
		style.PopupBorderSize = 1.0f;
		style.FrameRounding = 0.0f;
		style.FrameBorderSize = 1.0f;
		style.ItemSpacing = ImVec2(7.0f, 7.0f);
		style.ItemInnerSpacing = ImVec2(7.0f, 7.099999904632568f);
		style.CellPadding = ImVec2(6.0f, 6.0f);
		style.IndentSpacing = 6.0f;
		style.ColumnsMinSpacing = 6.0f;
		style.ScrollbarSize = 13.19999980926514f;
		style.ScrollbarRounding = 1.200000047683716f;
		style.GrabMinSize = 10.0f;
		style.GrabRounding = 0.0f;
		style.TabRounding = 0.0f;
		style.TabBorderSize = 0.0f;
		style.ColorButtonPosition = ImGuiDir_Right;
		style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
		style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

		auto& colors{ style.Colors };
		colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
		colors[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.52f, 0.52f, 1.00f);
		colors[ImGuiCol_WindowBg] = ImVec4(0.13f, 0.13f, 0.14f, 1.00f);
		colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		colors[ImGuiCol_PopupBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
		colors[ImGuiCol_Border] = ImVec4(0.00f, 0.00f, 0.00f, 0.22f);
		colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.13f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
		colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
		colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
		colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
		colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
		colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
		colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.40f, 0.54f);
		colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.64f, 0.34f, 1.00f, 1.00f);
		colors[ImGuiCol_CheckMark] = ImVec4(0.64f, 0.34f, 1.00f, 1.00f);
		colors[ImGuiCol_CheckboxSelectedBg] = ImVec4(0.64f, 0.34f, 1.00f, 0.21f);
		colors[ImGuiCol_SliderGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
		colors[ImGuiCol_Button] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
		colors[ImGuiCol_Header] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.00f, 0.00f, 0.36f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.22f, 0.23f, 0.33f);
		colors[ImGuiCol_Separator] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
		colors[ImGuiCol_SeparatorHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
		colors[ImGuiCol_SeparatorActive] = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
		colors[ImGuiCol_ResizeGrip] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
		colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
		colors[ImGuiCol_ResizeGripActive] = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
		colors[ImGuiCol_InputTextCursor] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
		colors[ImGuiCol_Tab] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
		colors[ImGuiCol_TabSelected] = ImVec4(0.20f, 0.20f, 0.20f, 0.36f);
		colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.64f, 0.34f, 1.00f, 1.00f);
		colors[ImGuiCol_TabDimmed] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
		colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
		colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.50f, 0.50f, 0.50f, 0.00f);
		colors[ImGuiCol_DockingPreview] = ImVec4(0.64f, 0.34f, 1.00f, 0.70f);
		colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
		colors[ImGuiCol_PlotLines] = ImVec4(0.71f, 0.47f, 1.00f, 1.00f);
		colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.64f, 0.34f, 1.00f, 1.00f);
		colors[ImGuiCol_PlotHistogram] = ImVec4(0.71f, 0.47f, 1.00f, 1.00f);
		colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.64f, 0.34f, 1.00f, 1.00f);
		colors[ImGuiCol_TableHeaderBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
		colors[ImGuiCol_TableBorderStrong] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
		colors[ImGuiCol_TableBorderLight] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
		colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
		colors[ImGuiCol_TextLink] = ImVec4(0.64f, 0.34f, 1.00f, 1.00f);
		colors[ImGuiCol_TextSelectedBg] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
		colors[ImGuiCol_TreeLines] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
		colors[ImGuiCol_DragDropTarget] = ImVec4(0.64f, 0.34f, 1.00f, 1.00f);
		colors[ImGuiCol_DragDropTargetBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		colors[ImGuiCol_UnsavedMarker] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
		colors[ImGuiCol_NavCursor] = ImVec4(0.77f, 0.59f, 1.00f, 1.00f);
		colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.64f, 0.34f, 1.00f, 1.00f);
		colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.64f, 0.34f, 1.00f, 0.44f);
		colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.64f, 0.34f, 1.00f, 0.44f);
	}
}