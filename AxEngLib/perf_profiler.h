#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ax
{
	static constexpr size_t kProfilerSampleCount = 1000;

	struct PerfStats
	{
		double averageMs{0.0};
		double minMs{0.0};
		double maxMs{0.0};
		size_t samples{0};
	};

	class ProfilerSegment
	{
	public:
		ProfilerSegment();
		void addSample(std::chrono::nanoseconds ns);
		PerfStats stats() const;

	private:
		mutable std::mutex m_mutex;
		std::array<uint64_t, kProfilerSampleCount> m_samples{}; // nanoseconds
		size_t m_index{0};
		size_t m_count{0};
	};

	class Profiler
	{
	public:
		static Profiler& instance();

		std::shared_ptr<ProfilerSegment> segment(const std::string& name);
		std::vector<std::pair<std::string, std::shared_ptr<ProfilerSegment>>> all_segments();

		class ScopedSample
		{
		public:
			ScopedSample(std::shared_ptr<ProfilerSegment> seg);
			~ScopedSample();
		private:
			std::shared_ptr<ProfilerSegment> m_seg;
			std::chrono::high_resolution_clock::time_point m_start;
		};

	private:
		std::mutex m_mutex;
		std::unordered_map<std::string, std::shared_ptr<ProfilerSegment>> m_segments;
	};
}
