#include "custom_type.h"

void ax::type::TypeGenerator::for_each_type(std::function<void(const std::string&, FieldType, size_t)> func)
{
	for (const auto& [name, fieldType] : s_field_name_to_type)
	{
		auto sizeIt{ s_field_type_sizes.find(fieldType) };
		if (sizeIt != s_field_type_sizes.end())
			func(name, fieldType, sizeIt->second);
		else
			spdlog::error("Failed to find size for type: {}", name);
	}
}

size_t ax::type::TypeGenerator::get_size(FieldType t)
{
	return TypeGenerator::s_field_type_sizes[t];
}

void ax::type::TypeGenerator::register_type(const std::string& name, const TypeDef& def)
{
	spdlog::info("Registering type: {}, size: {} bytes", name, def.overall_size);
	m_types.emplace(name, def);
}

ax::type::Pool ax::type::TypeGenerator::create_pool(const std::string& name, size_t count)
{
	return { m_types[name], count };
}

ax::type::PoolView::PoolView(Pool& pool)
	: m_pool{ pool }
	, m_data_start{ pool.m_data.data() }
	, m_data_size{ pool.m_data.size() }
{
}

ax::type::Pool::Pool(const TypeDef& def, size_t objectCount)
	: m_types{ def }
	, m_data{}
{
	m_data.resize(objectCount * def.overall_size);
}
