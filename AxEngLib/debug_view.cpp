#include "debug_view.h"

#include <imgui.h>

#undef min
#undef max
#include "imgui_zoomable_image.h"

void ax::debug::View::register_debug_view(ax::Application& app)
{
	s_toggleHandler.subscribe
	(
		[](const ax::input::KeyEvent& e)
		{
			if (e.pressed)
				toggle();
		}
	);

	app.window()->get_ui_event_handler().subscribe
	(
		[&app](const ax::WindowUIEvent& e)
		{
			if (!s_enabled)
				return;

			ImGui::ShowDemoWindow();

			ImGui::BeginMainMenuBar();
			{
				ImGui::Text("AxEng");

				if (ImGui::Button("Reload Pipeline"))
				{
					app.window()->reload_pipeline();
				}

				ImGuiIO& io = ImGui::GetIO();
				ImGui::SameLine(ImGui::GetWindowWidth() - 335);
				ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
			}
			ImGui::EndMainMenuBar();

			ImGui::Begin("Timing");
			{
				static float deltaTimes[512]{ 0 };
				static std::size_t deltaPtr = 0;
				deltaTimes[deltaPtr++] = (float)e.delta * 1000.0f;
				deltaPtr %= 512;
				ImGui::PlotHistogram("Delta Times (ms)", deltaTimes, 512);
			}
			ImGui::End();

			ImGui::Begin("Texture View");
			{
				static Texture* texture{ nullptr };
				static std::string label{ "Pick a texture to preview" };
				static bool filter{ false };
				static ImGuiImage::State zoomState;

				if (ImGui::BeginCombo("##", label.c_str()))
				{
					static size_t current{ 0 };
					size_t n{ 0 };
					app.textures().for_each_name
					(
						[&n, &app](const std::string& name)
						{
							if (ImGui::Selectable(name.c_str(), current == n))
							{
								texture = app.textures().get(name);
								current = n;
								label = name;
								zoomState = {};
								zoomState.textureSize = ImVec2((float)texture->width(), (float)texture->height());
								zoomState.maintainAspectRatio = true;
							}
							n++;
						}
					);

					ImGui::EndCombo();

				}
				ImGui::SameLine();
				ImGui::Checkbox("Filter", &filter);


				if (texture != nullptr)
				{
					if (!filter)
						ImGui::GetWindowDrawList()->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest);

					ImVec2 displaySize = ImGui::GetContentRegionAvail();
					if (texture == nullptr)
						spdlog::error("Unable to load texture 'tower'");
					else
						ImGuiImage::Zoomable((ImTextureID)(intptr_t)texture->view().Get(), displaySize, &zoomState);
				}
			}
			ImGui::End();
		}
	);
}

void ax::debug::View::toggle()
{
	s_enabled = !s_enabled;
}

void ax::debug::View::enable()
{
	s_enabled = true;
}

void ax::debug::View::disable()
{
	s_enabled = false;
}
