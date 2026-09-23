#include "debug_view.h"

#include <imgui.h>
#include <cstring>
#include "perf_profiler.h"

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

			ImGui::Begin("Renderer");
			{
				if (ImGui::Button("Reload Pipeline"))
				{
					app.window()->reload_pipeline();
				}
			}
			ImGui::End();

			ImGui::Begin("Timing");
			{
				static float deltaTimes[512]{ 0 };
				static std::size_t deltaPtr = 0;
				deltaTimes[deltaPtr++] = (float)e.delta * 1000.0f;
				deltaPtr %= 512;
				ImGui::PlotHistogram("Delta Times (ms)", deltaTimes, 512);

			// Dynamic profiler flame-graph style display
			{
				auto& prof = ax::Profiler::instance();
				auto all = prof.all_segments();
				if (!all.empty())
				{
					// Build a list of stats and sort by average desc
					struct Item { std::string name; PerfStats s; };
					std::vector<Item> items;
					items.reserve(all.size());
					for (auto &kv : all)
					{
						Item it;
						it.name = kv.first;
						it.s = kv.second->stats();
						items.push_back(std::move(it));
					}
					std::sort(items.begin(), items.end(), [](auto &a, auto &b){ return a.s.averageMs > b.s.averageMs; });

					// Determine frame reference (use "frame" if present, otherwise max avg)
					double frameRef = 0.0;
					for (auto &it : items) if (it.name == "frame") { frameRef = it.s.averageMs; break; }
					if (frameRef <= 0.0)
					{
						for (auto &it : items) frameRef = std::max(frameRef, it.s.averageMs);
						if (frameRef <= 0.0) frameRef = 1.0; // avoid divide by zero
					}

					ImDrawList* dl = ImGui::GetWindowDrawList();
					const ImVec2 pos = ImGui::GetCursorScreenPos();
					const float availX = ImGui::GetContentRegionAvail().x;
					const float barHeight = 20.0f;

					// Background bar
					dl->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + availX, pos.y + barHeight), ImGui::GetColorU32(ImGuiCol_FrameBg));

					float curX = pos.x;
					int idx = 0;
					for (auto &it : items)
					{
						if (it.name == "frame")
							continue;
						// fraction relative to frameRef
						double frac = it.s.averageMs / frameRef;
						if (frac <= 0.0) continue;
						float w = static_cast<float>(frac * availX);
						// color variation
						ImU32 col = ImGui::GetColorU32(ImVec4(0.2f + 0.6f * (idx % 7) / 7.0f, 0.4f, 0.6f, 1.0f));
						dl->AddRectFilled(ImVec2(curX, pos.y), ImVec2(curX + w, pos.y + barHeight), col);
						// label
						char buf[128];
						snprintf(buf, sizeof(buf), "%s: %.3fms", it.name.c_str(), it.s.averageMs);
						dl->AddText(ImVec2(curX + 4.0f, pos.y + 2.0f), ImGui::GetColorU32(ImGuiCol_Text), buf);

						// hover tooltip
						ImVec2 mousePos = ImGui::GetIO().MousePos;
						if (mousePos.x >= curX && mousePos.x <= curX + w && mousePos.y >= pos.y && mousePos.y <= pos.y + barHeight)
						{
							ImGui::SetTooltip("%s\navg=%.3fms min=%.3fms max=%.3fms samples=%zu", it.name.c_str(), it.s.averageMs, it.s.minMs, it.s.maxMs, it.s.samples);
						}

						curX += w;
						idx++;
					}

					// advance cursor
					ImGui::Dummy(ImVec2(availX, barHeight + 4.0f));
				}
			}
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

				ImGui::PushItemWidth(-1);
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
				else
				{
					// Reset if we didn't set the width of the list box (because it was hidden)
					ImGui::PopItemWidth();
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
