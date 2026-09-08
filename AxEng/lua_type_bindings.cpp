#include "lua_bindings.h"
#include "custom_type.h"

static void setup_type_bindings(sol::state& env)
{
	auto type_table = env.create_table();
	auto type_size_table = env.create_table();

	ax::type::TypeGenerator::ForEachType
	(
		[&type_table, &type_size_table](const std::string& name, ax::type::FieldType type, size_t size)
		{
			type_table[name] = static_cast<uint32_t>(type);
			type_size_table[name] = size;
		}
	);

	env["type"] = type_table;
	env["type_size"] = type_size_table;
}

static bool registered = ax::lua::bindings::register_binding("type", &setup_type_bindings);
