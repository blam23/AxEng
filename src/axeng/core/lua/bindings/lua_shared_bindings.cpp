#include "axeng/core/lua/bindings/lua_shared_bindings.h"
#include "axeng/core/application.h"

void ax::lua::bindings::setup_shared_bindings(ax::Application& app, sol::state& state, bool mainThread)
{
	auto app_table{ state["app"].get<sol::table>() };
	app_table["get_or_create_shared"] =
		[&app](const std::string& name) -> ax::lua::SharedObject*
		{
			std::lock_guard lock{ app.m_shared_mutex };
			if (auto it = app.m_shared.find(name); it != app.m_shared.end())
			{
				it->second.increment_ref_count();
				return &it->second;
			}
			auto it{ app.m_shared.try_emplace(name).first };
			it->second.increment_ref_count();
			return &it->second;
		};

	app_table["release_shared"] =
		[&app](ax::lua::SharedObject* so)
		{
			std::lock_guard lock{ app.m_shared_mutex };
			const auto count{ so->decrement_ref_count() };
			if (count == 0)
			{
				for (auto it{ app.m_shared.begin() }; it != app.m_shared.end(); ++it)
				{
					if (&it->second == so)
					{
						app.m_shared.erase(it);
						break;
					}
				}
			}
		};

	app_table["signal"] =
		[](ax::lua::SharedObject& object, const std::string& signalName)
		{
			object.signal(signalName);
		};

	app_table["on_signal"] =
		[&app, mainThread](ax::lua::SharedObject& object, const std::string& signalName, sol::protected_function function)
		{
			object.on_signal(mainThread ? &app : nullptr, signalName, function);
		};

	state.new_usertype<ax::lua::SharedObject>
	(
		"shared",
		"set", &ax::lua::SharedObject::set,
		"get", [&state](ax::lua::SharedObject& object, const std::string& key)
		{
			return object.get(state, key);
		}
	);
}
