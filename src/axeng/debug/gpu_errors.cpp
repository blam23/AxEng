#include "axeng/debug/gpu_errors.h"

#include <mutex>
#include <utility>

namespace ax::gpu
{
	namespace
	{
		std::mutex s_mutex;
		ErrorStats s_errors;
	}

	void record_error(std::string_view message)
	{
		const std::scoped_lock lock{ s_mutex };
		if (s_errors.count++ == 0) s_errors.first = std::string{ message };
	}

	ErrorStats take_errors()
	{
		const std::scoped_lock lock{ s_mutex };
		return std::exchange(s_errors, {});
	}
}
