#pragma once

// AxEng
#include "helpers.h"
#include "scripting.h"
#include "resource_loader.h"

namespace ax
{
	enum class ModuleType
	{
		Invalid     = 0,
		Application = 1,
		Library     = 2,
	};

	struct ModuleDefinition
	{
		std::string name;
		ModuleType moduleType;
		std::unique_ptr<IResourceLoader> loader;
	};

	class Module final
	{
	public:
		DISABLE_COPY_AND_MOVE(Module);

		Module(const ModuleDefinition&);
	};
}