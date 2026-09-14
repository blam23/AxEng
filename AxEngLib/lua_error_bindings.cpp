#pragma once

#include "lua_error_bindings.h"
#include "error.h"
#include "error_def.h"

void ax::lua::bindings::setup_error_bindings(sol::state& state)
{
	auto error_table = state.create_table();

#define ERR(n, i) error_table[#n] = std::to_underlying(ax::Error::##n);
	ERRORS
#undef ERR

	state["error_code"] = error_table;
}

#undef ERRORS