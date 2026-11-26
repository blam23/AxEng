#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Counts the WebGPU validation errors the device reports (uncaptured errors and device loss: an invalid shader, a
// pipeline that failed to build), so self-tests can fail on them rather than leave them in a log nobody reads.
namespace ax::gpu
{
	struct ErrorStats
	{
		std::uint64_t count{};
		// The first error reported since the count was last taken (empty if none).
		std::string first{};
	};

	void record_error(std::string_view message);

	// The errors since the last call, which starts the count again.
	ErrorStats take_errors();

	// Whether the device has been lost (other than destroyed on purpose): nothing drawn reaches the screen again.
	void set_device_lost(bool lost);
	bool device_lost();
}
