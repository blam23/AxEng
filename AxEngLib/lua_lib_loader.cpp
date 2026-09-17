#include "lua_lib_loader.h"
#include "resource_loader.h"
#include "script.h"

void ax::lua::libs::register_embedded()
{
	ax::Resource::register_embedded_resource<ax::lua::Script>("@init", get("@init"));
	ax::Resource::register_embedded_resource<ax::lua::Script>("@std", get("@std"));
	ax::Resource::register_embedded_resource<ax::lua::Script>("@compiler_core", get("@compiler_core"));
	ax::Resource::register_embedded_resource<ax::lua::Script>("@json", get("@json"));
}
