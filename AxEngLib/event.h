#pragma once

#include <map>
#include <functional>
#include <mutex>

#include "helpers.h"

namespace ax
{
	template <typename T_EVENT>
	class EventHandler
	{
		DISABLE_COPY_AND_MOVE(EventHandler<T_EVENT>);

		using T_FUNC = std::function<void(const T_EVENT&)>;

	public:
		EventHandler<T_EVENT>() = default;
		~EventHandler<T_EVENT>() = default;

		const size_t subscribe(T_FUNC&& handler)
		{
			std::unique_lock<std::mutex> lock{ m_subscriptionMutex };
			m_subscriptions.emplace(m_nextIdx, handler);
			return m_nextIdx++;
		}

		const void unsubscribe(size_t id)
		{
			std::unique_lock<std::mutex> lock{ m_subscriptionMutex };
			m_subscriptions.erase(id);
		}
		
		void fire(T_EVENT&& eventData) const
		{
			std::unique_lock<std::mutex> lock{ m_subscriptionMutex };
			for (auto& handler : m_subscriptions)
				handler.second(eventData);
		}

	private:
		std::map<size_t, T_FUNC> m_subscriptions{};
		mutable std::mutex m_subscriptionMutex{};
		std::size_t m_nextIdx;
	};
};