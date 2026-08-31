#pragma once

#include <vector>
#include <functional>
#include <mutex>

#include "helpers.h"

namespace ax
{
	template <typename T_EVENT>
	class EventHandler
	{
		DISABLE_COPY_AND_MOVE(EventHandler<T_EVENT>);

		using T_FUNC = std::function<void(T_EVENT&)>;

	public:
		EventHandler<T_EVENT>() = default;
		~EventHandler<T_EVENT>() = default;

		T_FUNC& subscribe(T_FUNC&& handler)
		{
			std::unique_lock<std::mutex> waitLock{ m_subscriptionMutex };
			m_subscriptions.emplace_back(handler);
			return m_subscriptions[m_subscriptions.size() - 1];
		}

		void unsubscribe(const T_FUNC& handler)
		{
			std::unique_lock<std::mutex> waitLock{ m_subscriptionMutex };
			m_subscriptions.erase(handler);
		}
		
		void fire(T_EVENT&& eventData)
		{
			for (auto& handler : m_subscriptions)
				handler(eventData);
		}

	private:
		std::vector<T_FUNC> m_subscriptions{};
		std::mutex m_subscriptionMutex{};
	};
};