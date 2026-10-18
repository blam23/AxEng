#pragma once

#include "axeng/core/lua/lua_bindings.h"

namespace ax::lua::bindings
{
	// Binds app.audio. Only called on the main thread when the application has an audio system.
	void setup_audio_bindings(ax::Application&, sol::state&);
}
