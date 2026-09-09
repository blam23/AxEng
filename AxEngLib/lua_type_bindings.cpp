#include "lua_bindings.h"
#include "custom_type.h"

static ax::type::TypeDef type_from_table(const sol::table& tbl)
{
	ax::type::TypeDef def{};
	size_t offset{ 0 };

	for (const auto& kvp : tbl)
	{
		size_t size{ ax::type::TypeGenerator::get_size(static_cast<ax::type::FieldType>(kvp.second.as<uint32_t>())) };
		def.field_map.emplace(kvp.first.as<std::string_view>(), ax::type::FieldDef{ offset, size });
		offset += size + 1;
	}

	def.overall_size = offset;
	return def;
};

static void setup_type_bindings(sol::state& env)
{
	auto type_table = env.create_table();
	auto type_size_table = env.create_table();

	ax::type::TypeGenerator::for_each_type
	(
		[&type_table, &type_size_table](const std::string& name, ax::type::FieldType type, size_t size)
		{
			type_table[name] = static_cast<uint32_t>(type);
			type_size_table[name] = size;
		}
	);

	type_table["define"] = &type_from_table;

	env["type"] = type_table;
	env["type_size"] = type_size_table;
}

static bool registered = ax::lua::bindings::register_binding("type", &setup_type_bindings);
