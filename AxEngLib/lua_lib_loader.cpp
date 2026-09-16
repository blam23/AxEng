#include "lua_lib_loader.h"
#include "resource_loader.h"
#include "script.h"

#define EMBEDDED_STR_RESOURCE(name) { ax::lua::libs::##name, sizeof(ax::lua::libs::##name)-1 }

void ax::lua::libs::register_embedded()
{
	ax::Resource::register_embedded_resource<ax::lua::Script>("@init", EMBEDDED_STR_RESOURCE(init));
	ax::Resource::register_embedded_resource<ax::lua::Script>("@std", EMBEDDED_STR_RESOURCE(std_lib));
	ax::Resource::register_embedded_resource<ax::lua::Script>("@compiler_core", EMBEDDED_STR_RESOURCE(compiler_core));
	ax::Resource::register_embedded_resource<ax::lua::Script>("@json", EMBEDDED_STR_RESOURCE(json));
}
