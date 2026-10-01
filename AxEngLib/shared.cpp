#include "shared.h"

#include "application.h"

#include "glm/glm.hpp"


sol::object copy_object(const sol::object& in, sol::state& state)
{
	const auto type{ in.get_type() };

	switch (type)
	{
		case sol::type::string:   return sol::make_object(state, in.as<std::string>());
		case sol::type::number:   return sol::make_object(state, in.as<double>());
		case sol::type::boolean:  return sol::make_object(state, in.as<bool>());

		case sol::type::userdata:
		{
			if (in.is<glm::vec2>()) {
				const auto& in_vec{ in.as<glm::vec2>() };
				return sol::make_object<glm::vec2>(state, in_vec.x, in_vec.y);
			}
			return {};
		}

		case sol::type::table:
		{
			auto orig = in.as<sol::table>();
			auto clone = state.create_table();

			for (const auto& [k, v] : orig)
				clone.set(copy_object(k, state), copy_object(v, state));

			return clone;
		}

		// todo?
		case sol::type::function:
		case sol::type::lightuserdata:

		// ignore
		case sol::type::thread:
		case sol::type::none:
		case sol::type::lua_nil:
		case sol::type::poly:
		default:
			return {};
	}
}

void ax::lua::SharedObject::on_signal(Application* app, const std::string& key, sol::protected_function func)
{
	std::lock_guard lock{ m_sharedStateMutex };

	auto it = m_signals.try_emplace(key).first;

	if (app == nullptr) // can run on this thread, call func directly
	{
		it->second.subscribe
		(
			[func](int)
			{
				sol::protected_function_result result{ func() };
				if (!result.valid())
				{
					const sol::error err = result;
					spdlog::error("Error in signal callback: {}", err.what());
				}
			}
		);
	}
	else // can't run on this thread, schedule func call
	{
		it->second.subscribe
		(
			[func, app](int)
			{
				app->call_deferred
				(
					[func]
					{
						sol::protected_function_result result{ func() };
						if (!result.valid())
						{
							const sol::error err = result;
							spdlog::error("Error in signal callback: {}", err.what());
						}
					}
				);
			}
		);
	}
}

sol::object ax::lua::SharedObject::copy_object_to_shared(const sol::object& in)
{
	std::lock_guard lock{ m_sharedStateMutex };
	return copy_object(in, m_sharedState);
}

sol::object ax::lua::SharedObject::copy_object_from_shared(sol::state& copy_to, const sol::object& in)
{
	std::lock_guard lock{ m_sharedStateMutex };
	return copy_object(in, copy_to);
}

