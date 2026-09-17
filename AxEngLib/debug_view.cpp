#include "debug_view.h"

#include <imgui.h>
#include <cstring>

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

			//ImGui::ShowDemoWindow();

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

			ImGui::Begin("Lua Debug");
			{
				const auto& state{ app.scripts().debug_get_state({}) };
				const auto& env{ app.debug_get_env({}) };
				const sol::table& global_table{ state["_G"].get<sol::table>() };
				static std::string picked_key{ "" };
				static bool is_global{ false };
				static std::string label{ "Pick a script to inspect" };

				ImGui::SetNextItemWidth(-1);
				if (ImGui::BeginListBox("##lua_vars"))
				{
					static size_t current{ 0 };
					size_t n{ 0 };
					
					// Env vars
					for (const auto& kvp : env)
					{
						const auto& key{ kvp.first.as<std::string>() };
						if (ImGui::Selectable(key.c_str(), current == n))
						{
							picked_key = key;
							label = key;
							current = n;
							is_global = false;
						}
						n++;
					}

					// Global vars
					for (const auto& kvp : global_table)
					{
						const auto& key{ kvp.first.as<std::string>() };
						if (ImGui::Selectable(key.c_str(), current == n))
						{
							picked_key = key;
							label = key;
							current = n;
							is_global = true;
						}
						n++;
					}

					ImGui::EndListBox();
				}

				if (picked_key.length() > 0)
				{
					sol::object value;

					if (is_global)
						value = global_table[picked_key];
					else
						value = env[picked_key];

					const auto type{ value.get_type() };
					switch (type)
					{
					case sol::type::nil:
						ImGui::Text("Value: nil");
						break;
					case sol::type::number:
						ImGui::Text("Value: %f", value.as<float>());
						break;
					case sol::type::string:
						ImGui::Text("Value: '%s'", value.as<std::string>().c_str());
						break;
					case sol::type::boolean:
						ImGui::Text("Value: '%s'", value.as<bool>() ? "true" : "false");
						break;
					case sol::type::function:
						ImGui::Text("Value: <function>");
						break;
					case sol::type::userdata:
					case sol::type::lightuserdata:
						ImGui::Text("Value: <user_data>");
						break;
					case sol::type::thread:
						ImGui::Text("Value: <thread>");
						break;
					case sol::type::table: // todo: recurse into table
						ImGui::Text("Value: <table>");
						break;
					default:
						ImGui::Text("Value: <unknown>");
						break;
					}
				}
			}
			ImGui::End();

			ImGui::Begin("Script View");
			{
				static lua::Script* script{ nullptr };
				static std::string label{ "Pick a script to inspect" };

				if (ImGui::BeginCombo("##scripts", label.c_str()))
				{
					static size_t current{ 0 };
					size_t n{ 0 };
					app.scripts().for_each_name
					(
						[&n, &app](const std::string& name)
						{
							if (ImGui::Selectable(name.c_str(), current == n))
							{
								script = app.scripts().get(name);
								current = n;
								label = name;
							}
							n++;
						}
					);

					ImGui::EndCombo();
				}

				if (script != nullptr)
				{
					const auto code_str{ script->code().c_str() };
					if (std::strcmp("LuaU", code_str) == 0)
						ImGui::Text("<Bytecode>");
					else
						ImGui::Text("%s", code_str); // explicitly use %s so any percent symbols in code_str don't create odd formatting errors
				}
			}
			ImGui::End();

			ImGui::Begin("Texture View");
			{
				static Texture* texture{ nullptr };
				static std::string label{ "Pick a texture to inspect" };
				static bool filter{ false };
				static ImGuiImage::State zoomState;

				if (ImGui::BeginCombo("##textures", label.c_str()))
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
