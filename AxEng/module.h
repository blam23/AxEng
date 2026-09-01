#pragma once

// AxEng
#include "helpers.h"
#include "lua_engine.h"
#include "script.h"
#include "resource_loader.h"

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
	struct ModuleDefinition
	{
		std::string name;
		ModuleType moduleType;
		T_Loader loader;
	};

	template <typename T_Loader>
		requires ValidLoader<T_Loader>
	class Module final
	{
	public:
		DISABLE_COPY_AND_MOVE(Module<T_Loader>);

		Module(ModuleDefinition<T_Loader>& def)
			: m_name{ def.name }
			, m_loader{ std::move(def.loader) }
			, m_type{ def.moduleType }
		{
		}
		
		T_Loader& loader() { return m_loader; }

	private:
		const std::string m_name;
		const ModuleType m_type;
		T_Loader m_loader;
	};

	using FileSystemModuleDefinition = ModuleDefinition<FileResourceLoader>;
	using FileSystemModule = Module<FileResourceLoader>;
}