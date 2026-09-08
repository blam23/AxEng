#pragma once

#include "helpers.h"
#include <map>
#include <typeinfo>
#include <string>
#include "glm/glm.hpp"
#include "lua_bindings.h"
#include "spdlog/spdlog.h"

namespace ax::type
{
	#define FIELDS \
		FIELD("float",      t_float,       float,         1) \
		FIELD("double",     t_double,      double,        2) \
		FIELD("uint32",     t_uint32,      uint32_t,      3) \
		FIELD("int32",      t_int32,       int32_t,       4) \
		FIELD("vec2",       t_vec2,        glm::vec2,     5) \
		FIELD("string",     t_string,      std::string,   6) \
		FIELD("id",         t_resourceID,  std::string,   7)

	#define FIELD(name, type, cpp_type, value) type = value,
	enum class FieldType
	{
		FIELDS
	};
	#undef FIELD

	class TypeGenerator
	{
	public:
		DISABLE_COPY_AND_MOVE(TypeGenerator);

		static void ForEachType(std::function<void(const std::string&, FieldType, size_t)> func)
		{
			for (const auto& [name, fieldType] : s_field_name_to_type)
			{
				auto sizeIt{ s_field_type_sizes.find(fieldType) };
				if (sizeIt != s_field_type_sizes.end())
				{
					func(name, fieldType, sizeIt->second);
				}
				else
				{
					spdlog::error("Failed to find size for type: {}", name);
				}
			}
		}

		static std::string GetTypeName(FieldType type)
		{
			for (const auto& [name, fieldType] : s_field_name_to_type)
			{
				if (fieldType == type)
					return name;
			}
			return std::string{};
		}

	private:
		#define FIELD(name, type, cpp_type, value) {FieldType::##type, sizeof(cpp_type) },
		inline static std::map<FieldType, size_t> s_field_type_sizes
		{
			FIELDS
		};
		#undef FIELD

		#define FIELD(name, type, cpp_type, value) {name, FieldType::type},
		inline static std::map<std::string, FieldType> s_field_name_to_type
		{
			FIELDS
		};
		#undef FIELD
	};

	#undef FIELDS
}

