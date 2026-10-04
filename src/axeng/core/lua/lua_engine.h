#pragma once

#include "axeng/core/helpers.h"
#include "axeng/core/resource_loader.h"
#include "axeng/core/error.h"

#include <string>

#define SOL_ALL_SAFETIES_ON 1
#define SOL_PRINT_ERRORS 1
#include <sol/sol.hpp>
#include "axeng/external/flag_set.hpp"

namespace ax::lua
{
	enum class Permission : uint32_t
	{
		IO,
		OS,
		FileNotify,
		Threads,
		
		_
	};

	Permission get_perm_from_string(const std::string& perm);

	class Manager
	{
	public:
		DISABLE_COPY_AND_MOVE(Manager);

		Manager(flag_set<Permission> requestedPermissions);
		~Manager();
		 
		ax::Error setup();
		ax::Error cleanup();

		sol::environment create_env();
		sol::load_result load(const std::string& code, const std::string& file);

		sol::state& state() { return m_state; }
		const sol::state& state() const { return m_state; }

		bool has_permission(Permission) const;

	private:
		sol::state m_state;
		bool m_loaded{ false };
		flag_set<Permission> m_permissionFlags;
	};
}