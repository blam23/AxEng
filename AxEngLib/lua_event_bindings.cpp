#include "lua_event_bindings.h"

#include "lua_engine.h"
#include "event.h"

void ax::lua::bindings::setup_event_bindings(sol::state& state)
{
    using EH = ax::EventHandler<sol::object>;

	auto ret = state.new_usertype<EH>("lua_event");
    ret["subscribe"] = &EH::subscribe;
    ret["unsubscribe"] = &EH::unsubscribe;
    ret["fire"] = &EH::fire;
}
