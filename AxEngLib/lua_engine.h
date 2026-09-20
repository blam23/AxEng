#pragma once

#include "helpers.h"
#include "resource_loader.h"
#include "error.h"

#include <string>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
#include "flag_set.hpp"

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