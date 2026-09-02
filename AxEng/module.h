#pragma once

// AxEng
#include "helpers.h"
#include "lua_engine.h"
#include "script.h"
#include "resource_loader.h"
#include "log_timer.h"

namespace ax
{
	enum class ModuleType
	{
		Invalid     = 0,
		Application = 1,
		Library     = 2,
	};

	template <typename T_Loader>
		requires ValidLoader<T_Loader>
	class Module final
	{
	public:
		DISABLE_COPY_AND_MOVE(Module<T_Loader>);

		Module(std::string_view root)
			: m_loader{ T_Loader{ root } }
			, m_scripts{ m_loader }
		{
		}

		~Module()
		{
			m_scripts.unload_all();
		}

		bool try_load()
		{
			LogTimer _timer{ "Module Load" };

			ax::lua::Script* manifest{ m_scripts.load("!manifest", "manifest.lua") };
			auto env{ m_scripts.create_env() };

			if (!manifest)
			{
				spdlog::error("Failed to load manifest.lua");
				return false;
			}

			const auto& res{ manifest->run(env) };

			if (!res.valid())
			{
				const sol::error msg = res;
				spdlog::error("Failed to parse manifest.lua: {}", msg.what());
				return false;
			}

			const auto& app{ env["app"] };
			m_name = app["name"];
			m_type = static_cast<ModuleType>(app["type"]);

			std::string entryPointScript = app["entry_point"];
			m_entryPoint = m_scripts.load("!entry", entryPointScript);

			if (!m_entryPoint)
			{
				spdlog::error("Failed to load entry point script: '{}'", entryPointScript);
				return false;
			}

			m_loaded = true;
			return m_loaded;
		}
		
		T_Loader& loader() { return m_loader; }

	private:
		T_Loader m_loader;
		ax::lua::ScriptManager m_scripts;

		std::string m_name{};
		ax::lua::Script* m_entryPoint{};
		ModuleType m_type{ ModuleType::Invalid };

		bool m_loaded{ false };
	};

	using DirectoryModule = Module<DirectoryResourceLoader>;
}