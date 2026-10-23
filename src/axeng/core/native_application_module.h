#pragma once

#include "axeng/core/error.h"
#include "axeng/core/lua/lua_engine.h"

#include <webgpu/webgpu_cpp.h>

namespace ax
{
	class Application;

	class NativeApplicationModule
	{
	public:
		virtual ~NativeApplicationModule() = default;

		virtual ax::Error bind_lua(Application&, sol::state&) { return ax::Error::Success; }
		virtual ax::Error initialize(Application&) = 0;
		virtual ax::Error shutdown(Application&) { return ax::Error::Success; }
	};
}
