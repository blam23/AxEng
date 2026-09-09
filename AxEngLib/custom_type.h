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
		FIELD("id",         t_resourceID,  uint32_t,      7)

	#define FIELD(name, type, cpp_type, value) type = value,
	enum class FieldType
	{
		FIELDS
	};
	#undef FIELD

	struct FieldDef
	{
		size_t start;
		size_t size;
	};

	struct TypeDef
	{
		size_t overall_size;
		std::map<std::string, FieldDef> field_map;
	};

	class PoolView;
	class Pool
	{
	public:
		Pool(const TypeDef& def, size_t objectCount);

	private:
		std::vector<uint8_t> m_data;
		TypeDef m_types;
		friend PoolView;
	};

	class PoolView
	{
	public:
		PoolView(Pool& pool);

		template <typename T>
		T* get_field(size_t idx, const std::string& name)
		{
			const auto& itr{ m_pool.m_types.field_map.find(name) };
			if (itr != m_pool.m_types.field_map.end())
			{
				const auto offset{ idx * m_pool.m_types.overall_size };

				assert(sizeof(T) == itr->second.size);
				assert(offset + itr->second.start + itr->second.size <= m_data_size);

				return reinterpret_cast<T*>(m_data_start + offset + itr->second.start);
			}

			return nullptr;
		}

	private:
		uint8_t* m_data_start;
		size_t m_data_size;
		Pool& m_pool;
	};

	class TypeGenerator
	{
	public:
		DISABLE_COPY_AND_MOVE(TypeGenerator);

		TypeGenerator() = default;

		static void for_each_type(std::function<void(const std::string&, FieldType, size_t)> func);
		static size_t get_size(FieldType t);

		void register_type(const std::string& name, const TypeDef& def);
		Pool create_pool(const std::string& name, size_t count);

	private:
		std::map<std::string, TypeDef> m_types;

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

