#include "bind_all.h"

#include "lua_log_bindings.h"
#include "lua_type_bindings.h"
#include "lua_key_bindings.h"
#include "lua_window_bindings.h"

void ax::lua::bindings::setup()
{
	bindings::register_binding("type", &setup_type_bindings);

	bindings::register_binding("log", &setup_log_bindings);

	bindings::register_binding("keyboard", &setup_key_bindings);
	bindings::register_cleanup("keyboard", &cleanup_key_bindings);

	bindings::register_binding("window", &setup_window_bindings);
}

