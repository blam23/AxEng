#include "bind_all.h"
#include "lua_log_bindings.h"
#include "lua_type_bindings.h"

void ax::lua::bind_all()
{
	bindings::register_binding("type", &setup_type_bindings);
	bindings::register_binding("log", &setup_log_bindings);
}