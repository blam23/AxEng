#pragma once

#include "forward.h"
#include "application.h"
#include "resource_loader.h"
#include "log_timer.h"
#include "lua_engine.h"
#include "bind_all.h"
#include "script.h"
#include "window.h"

namespace ax
{
	void init();
	void teardown();
}