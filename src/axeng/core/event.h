#pragma once

#include <map>
#include <functional>
#include <mutex>

#include "axeng/core/helpers.h"

namespace ax
{
	using EventID = std::uint16_t; // suitable for converting to and from Lua
	template <typename T_EVENT>
	class EventHandler
	{
		DISABLE_COPY_AND_MOVE(EventHandler<T_EVENT>);

		using T_FUNC = std::function<void(const T_EVENT&)>;

	public:
		EventHandler<T_EVENT>() = default;
		~EventHandler<T_EVENT>() = default;

		EventID subscribe(T_FUNC&& handler)
		{
			std::unique_lock<std::mutex> lock{ m_subscriptionMutex };
			m_subscriptions.emplace(m_nextIdx, handler);
			return m_nextIdx++;
		}
		
		void unsubscribe(EventID id)
		{
			std::unique_lock<std::mutex> lock{ m_subscriptionMutex };
			m_subscriptions.erase(id);
		}

		void unsubscribe_all()
		{
			std::unique_lock<std::mutex> lock{ m_subscriptionMutex };
			m_subscriptions.clear();
		}
		
		void fire(T_EVENT&& eventData) const
		{
			std::unique_lock<std::mutex> lock{ m_subscriptionMutex };
			for (auto& handler : m_subscriptions)
				handler.second(eventData);
		}

	private:
		std::map<EventID, T_FUNC> m_subscriptions{};
		mutable std::mutex m_subscriptionMutex{};
		EventID m_nextIdx{ 0 };
	};
};