#pragma once

#include "helpers.h"
#include "resource_loader.h"
#include "error.h"

#include <string>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

namespace ax::lua
{
	class Manager
	{
	public:
		DISABLE_COPY_AND_MOVE(Manager);

		Manager() {};
		~Manager();

		ax::Error setup(const ResourceLoader&);
		ax::Error cleanup();

		sol::environment create_env();
		sol::load_result load(const std::string& code, const std::string& file);

		sol::state& state() { return m_state; }
		const sol::state& state() const { return m_state; }

	private:
		sol::state m_state;
		std::string m_initScript;
		bool m_loaded{ false };
	};
}