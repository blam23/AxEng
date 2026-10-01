#pragma once

#include "event.h"
#include "forward.h"
#include "helpers.h"
#include "lua_engine.h"

#include <map>
#include <string>

namespace ax::lua
{
	class SharedObject
	{
	public:
		DISABLE_COPY_AND_MOVE(SharedObject);

		SharedObject() {}

		void set(const std::string& key, sol::object value)
		{
			if (value == sol::nil)
				m_data.erase(key);
			else
				m_data[key] = copy_object_to_shared(value);
		}

		sol::object get(sol::state& copy_to, const std::string& key)
		{
			if (auto it = m_data.find(key); it != m_data.end())
				return copy_object_from_shared(copy_to, it->second);
			return sol::nil;
		}

		void increment_ref_count()
		{
			m_refCount++;
		}

		uint32_t decrement_ref_count()
		{
			if (m_refCount > 0)
				return --m_refCount;
			return 0;
		}

		void signal(const std::string& key)
		{
			std::lock_guard lock{ m_sharedStateMutex };

			if (auto it = m_signals.find(key); it != m_signals.end())
				it->second.fire(0);
		}

		void on_signal(Application* app, const std::string& key, sol::protected_function func);

	private:
		sol::state m_sharedState{};
		std::mutex m_sharedStateMutex{};

		std::map<std::string, sol::object> m_data{};
		std::atomic_uint32_t m_refCount{ 0 };

		sol::object copy_object_to_shared(const sol::object& in);
		sol::object copy_object_from_shared(sol::state& copy_to, const sol::object& in);

		std::map <std::string, EventHandler<int>>m_signals{};
	};
}