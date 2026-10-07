#pragma once

#include "axeng/core/forward.h"
#include "axeng/core/window_events.h"
#include "axeng/core/input/keyboard.h"
#include "axeng/core/lua/lua_engine.h"

namespace ax::debug
{
	class Debugger
	{
	public:
		DISABLE_COPY_AND_MOVE(Debugger);
		Debugger(Application& app) : m_app(app) {}
		~Debugger() = default;

		sol::state& get_state() const;
		sol::environment& get_env() const;

	private:
		Application& m_app;
	};

	using ViewCallback = std::function<void(Debugger& debugger, Application& app, const WindowUIEvent&)>;
	class View
	{
	public:
		DISABLE_COPY_AND_MOVE(View);
		View() = default;
		~View() = default;

		void register_view(Debugger& debugger, Application& app, input::Key toggleKey, const ViewCallback& callback);
		void toggle();
		void enable();
		void disable();

	private:
		bool m_enabled{ false };
		std::unique_ptr<input::KeyEventHandler> m_toggleKeyHandler;
	};

	class ViewManager
	{
	public:
		static void debug_view_callback(Debugger& debugger, Application& app, const WindowUIEvent& e);
		static void console_view_callback(Debugger& debugger, Application& app, const WindowUIEvent& e);
		static void setup_console_logging();
		static void teardown_console_logging();

		static void register_view(input::Key toggleKey, const ViewCallback& callback)
		{
			std::lock_guard<std::mutex> lock(m_mutex);

			if (!m_app || !m_debugger)
			{
				spdlog::error("<Debugger> Attempted to register view without an active application or debugger.");
				return;
			}

			m_views.emplace_back();
			m_views.back().register_view(*m_debugger, *m_app, toggleKey, callback);
		}

		static void setup(Application& app)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_debugger = std::make_unique<Debugger>(app);
			m_app = &app;
		}

		static void teardown()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_views.clear();
			m_debugger.reset();
			m_app = nullptr;
		}

	private:
		inline static std::mutex m_mutex;
		inline static std::list<View> m_views{};
		inline static std::unique_ptr<Debugger> m_debugger{ nullptr };
		inline static Application* m_app{ nullptr };
	};
}