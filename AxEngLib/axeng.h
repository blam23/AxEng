#pragma once

#include "forward.h"
#include "application.h"
#include "resource_loader.h"
#include "log_timer.h"
#include "lua_engine.h"
#include "bind_all.h"
#include "script.h"
#include "window.h"
#include "error.h"
#include "keyboard.h"

namespace ax
{
	void init();
	void teardown();

	ax::Error run(Application&&);
	ax::Error run_from_directory(std::string_view rootDirectory);
	ax::Error run_from_zip(std::string_view zipFile);
}