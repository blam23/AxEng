#pragma once

#include "axeng/core/lua/lua_bindings.h"
#include "axeng/core/particles/particle_system.h"

namespace ax::lua::bindings
{
	// Reads the fields of a particle description table (see docs/particles.md) over base. Throws sol::error for
	// invalid values.
	ax::ParticleConfig parse_particle_config(const sol::table&, ax::ParticleConfig base = {});
	void setup_particle_bindings(ax::Application&, sol::state&);
}
