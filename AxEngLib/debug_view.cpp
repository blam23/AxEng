#include "debug_view.h"

#include <algorithm>
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
				#ifdef _DEBUG
				ImGui::PushFont(nullptr, 24.f);
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "DEBUG BUILD");
				ImGui::PopFont();
				#endif
				constexpr std::size_t deltaCount = 512;
				static float deltaTimes[deltaCount]{ 0 };
				static std::size_t deltaPtr = 0;
				deltaTimes[deltaPtr++] = (float)e.delta * 1000.0f;
				deltaPtr %= deltaCount;
				ImGui::PlotHistogram("Delta Times (ms)", deltaTimes, deltaCount);

				double allTimes = 0.0;
				for (const auto& dt : deltaTimes)
					allTimes += dt;
				allTimes /= deltaCount;
				ImGui::Text("FPS Average: %.0f", 1000.0f / allTimes);

				#ifdef ENABLE_PROFILER
				{
					auto& prof = ax::Profiler::instance();
					auto roots = prof.all_segments();
					std::sort(roots.begin(), roots.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
					ImGui::TextUnformatted("Flame Graph");

					if (!roots.empty())
					{
						auto getDepth = 
							[&](auto&& self, const std::shared_ptr<ax::ProfilerSegment>& segment) -> size_t
							{
								size_t depth = 1;
								for (const auto& child : segment->children())
									depth = std::max(depth, 1 + self(self, child.second));
								return depth;
							};

						double totalRootMs = 0.0;
						size_t maxDepth = 0;
						for (const auto& root : roots)
						{
							totalRootMs += root.second->stats().averageMs;
							maxDepth = std::max(maxDepth, getDepth(getDepth, root.second));
						}

						if (totalRootMs > 0.0)
						{
							const ImVec2 graphPos = ImGui::GetCursorScreenPos();
							const float graphWidth = ImGui::GetContentRegionAvail().x;
							const float barHeight = 20.0f;
							const float barGap = 2.0f;
							ImDrawList* drawList = ImGui::GetWindowDrawList();

							auto drawFlameSegment = 
								[&](auto&& self, const std::string& name,
									const std::string& path,
									const std::shared_ptr<ax::ProfilerSegment>& segment,
									float x, float y, float width, size_t depth)
								{
									if (width < 1.0f)
										return;

									const auto stats = segment->stats();
									const ImVec2 min(x, y);
									const ImVec2 max(x + width, y + barHeight);
									const float variation = static_cast<float>((depth * 3) % 5) * 0.06f;
									const ImU32 color = ImGui::GetColorU32(ImVec4(0.75f, 0.35f + variation, 0.2f, 1.0f));
									drawList->AddRectFilled(min, max, color);
									drawList->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border));

									if (width > ImGui::CalcTextSize(name.c_str()).x + 8.0f)
										drawList->AddText(ImVec2(x + 4.0f, y + 2.0f), ImGui::GetColorU32(ImGuiCol_Text), name.c_str());

									if (ImGui::IsMouseHoveringRect(min, max))
										ImGui::SetTooltip("%s\navg=%.3f ms min=%.3f ms max=%.3f ms samples=%zu", path.c_str(), stats.averageMs, stats.minMs, stats.maxMs, stats.samples);

									auto children = segment->children();
									std::sort(children.begin(), children.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
									double childrenMs = 0.0;
									for (const auto& child : children)
										childrenMs += child.second->stats().averageMs;

									const double childTimeScale = std::max(stats.averageMs, childrenMs);
									if (childTimeScale <= 0.0)
										return;

									float childX = x;
									for (const auto& child : children)
									{
										const auto childStats = child.second->stats();
										const float childWidth = width * static_cast<float>(childStats.averageMs / childTimeScale);
										self(self, child.first, path + "/" + child.first, child.second, childX, y + barHeight + barGap, childWidth, depth + 1);
										childX += childWidth;
									}
								};

							float rootX = graphPos.x;
							for (const auto& root : roots)
							{
								const float rootWidth = graphWidth * static_cast<float>(root.second->stats().averageMs / totalRootMs);
								drawFlameSegment(drawFlameSegment, root.first, root.first, root.second, rootX, graphPos.y, rootWidth, 0);
								rootX += rootWidth;
							}

							ImGui::Dummy(ImVec2(graphWidth, static_cast<float>(maxDepth) * (barHeight + barGap)));
						}
						else
						{
							ImGui::TextUnformatted("No profiler samples yet.");
						}
					}
					else
					{
						ImGui::TextUnformatted("No profiler segments yet.");
					}
					ImGui::Separator();

					auto drawSegment = [&](auto&& self, const std::string& name,
							const std::shared_ptr<ax::ProfilerSegment>& segment,
							double topLevelMs, size_t depth) -> void
						{
							const auto stats = segment->stats();
							float intensity = topLevelMs > 0.0
								? std::clamp(static_cast<float>(stats.averageMs / topLevelMs), 0.0f, 1.0f)
								: 0.0f;

							if (depth == 0)
								intensity = 0.0f; // Don't color top-level segments

							const ImU32 cellColor = ImGui::GetColorU32(ImVec4(
								0.25f + 0.65f * intensity,
								0.28f * (1.0f - intensity),
								0.28f * (1.0f - intensity), 1.0f));


							ImGui::TableNextRow();
							ImGui::TableNextColumn();
							ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, cellColor);
							auto children = segment->children();
							const bool hasChildren = !children.empty();
							const ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_SpanFullWidth
								| (hasChildren ? ImGuiTreeNodeFlags_None : ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
							const bool isOpen = ImGui::TreeNodeEx(name.c_str(), nodeFlags);
							ImGui::TableNextColumn();
							ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, cellColor);
							ImGui::Text("%.3f", stats.averageMs);
							ImGui::TableNextColumn();
							ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, cellColor);
							ImGui::Text("%.3f", stats.minMs);
							ImGui::TableNextColumn();
							ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, cellColor);
							ImGui::Text("%.3f", stats.maxMs);

							if (hasChildren && isOpen)
							{
								std::sort(children.begin(), children.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
								for (const auto& child : children)
									self(self, child.first, child.second, topLevelMs, depth + 1);
								ImGui::TreePop();
							}
						};

					if (ImGui::BeginTable("Profiler segments", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
					{
						ImGui::TableSetupColumn("Segment");
						ImGui::TableSetupColumn("Avg (ms)");
						ImGui::TableSetupColumn("Min (ms)");
						ImGui::TableSetupColumn("Max (ms)");
						ImGui::TableHeadersRow();
						for (const auto& root : roots)
							drawSegment(drawSegment, root.first, root.second, root.second->stats().averageMs, 0);
						ImGui::EndTable();
					}
				}
				#endif
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

					auto getTypeName = [](sol::type type) -> const char*
						{
							switch (type)
							{
							case sol::type::nil: return "nil";
							case sol::type::number: return "number";
							case sol::type::string: return "string";
							case sol::type::boolean: return "boolean";
							case sol::type::function: return "function";
							case sol::type::userdata: return "userdata";
							case sol::type::lightuserdata: return "lightuserdata";
							case sol::type::thread: return "thread";
							case sol::type::table: return "table";
							default: return "unknown";
							}
						};

					int tableId = 0;
					auto drawValue = [&](auto&& self, const sol::object& object, size_t depth) -> void
						{
							switch (object.get_type())
							{
							case sol::type::nil:
								ImGui::TextUnformatted("nil");
								break;
							case sol::type::number:
								ImGui::Text("%.15g", object.as<double>());
								break;
							case sol::type::string:
								{
									const auto text = object.as<std::string>();
									ImGui::TextUnformatted(text.c_str());
								}
								break;
							case sol::type::boolean:
								ImGui::TextUnformatted(object.as<bool>() ? "true" : "false");
								break;
							case sol::type::function:
								ImGui::TextUnformatted("<function>");
								break;
							case sol::type::userdata:
								ImGui::TextUnformatted("<userdata>");
								break;
							case sol::type::lightuserdata:
								ImGui::TextUnformatted("<lightuserdata>");
								break;
							case sol::type::thread:
								ImGui::TextUnformatted("<thread>");
								break;
							case sol::type::table:
								{
									if (depth >= 8)
									{
										ImGui::TextUnformatted("<maximum table depth>");
										break;
									}

									ImGui::PushID(tableId++);
									const sol::table table = object.as<sol::table>();
									if (ImGui::BeginTable("Lua table", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
									{
										ImGui::TableSetupColumn("Key");
										ImGui::TableSetupColumn("Type");
										ImGui::TableSetupColumn("Value");
										ImGui::TableHeadersRow();

										for (const auto& entry : table)
										{
											const sol::object& key = entry.first;
											const sol::object& entryValue = entry.second;
											ImGui::TableNextRow();
											ImGui::TableNextColumn();
											switch (key.get_type())
											{
											case sol::type::string:
												{
													const auto text = key.as<std::string>();
													ImGui::TextUnformatted(text.c_str());
												}
												break;
											case sol::type::number:
												ImGui::Text("%.15g", key.as<double>());
												break;
											case sol::type::boolean:
												ImGui::TextUnformatted(key.as<bool>() ? "true" : "false");
												break;
											default:
												ImGui::Text("<%s key>", getTypeName(key.get_type()));
												break;
											}

											ImGui::TableNextColumn();
											ImGui::TextUnformatted(getTypeName(entryValue.get_type()));
											ImGui::TableNextColumn();
											self(self, entryValue, depth + 1);
										}

										ImGui::EndTable();
									}
									ImGui::PopID();
								}
								break;
							default:
								ImGui::TextUnformatted("<unknown>");
								break;
							}
						};

					ImGui::Text("Value (%s):", getTypeName(value.get_type()));
					drawValue(drawValue, value, 0);
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
