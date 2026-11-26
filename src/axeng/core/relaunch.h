#pragma once

#include <string>
#include <utility>
#include <vector>

namespace ax
{
	// Starts the application again with the same arguments and these environment variables set besides the current
	// ones: a new process on the desktop (the caller then quits this one), the page loaded again in a browser (the page
	// provides globalThis.axRelaunch, which keeps its files first). False when it could not be started.
	bool relaunch(const std::vector<std::pair<std::string, std::string>>& environment);
}
