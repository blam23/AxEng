#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef ENABLE_PROFILER
namespace ax
{
	static constexpr size_t PROFILER_SAMPLE_COUNT{ 100 };

	struct PerfStats
	{
		double averageMs{ 0.0 };
		double minMs{ 0.0 };
		double maxMs{ 0.0 };
		size_t samples{ 0 };
	};

	class ProfilerSegment
	{
	public:
		ProfilerSegment();
		void addSample(std::chrono::nanoseconds ns);
		PerfStats stats() const;
		std::shared_ptr<ProfilerSegment> segment(const std::string& name);
		std::vector<std::pair<std::string, std::shared_ptr<ProfilerSegment>>> children() const;

	private:
		mutable std::mutex m_mutex;
		std::array<uint64_t, PROFILER_SAMPLE_COUNT> m_samples{}; // nanoseconds
		size_t m_index{ 0 };
		size_t m_count{ 0 };
		mutable std::mutex m_childrenMutex;
		std::unordered_map<std::string, std::shared_ptr<ProfilerSegment>> m_children;
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

#define PROFILER_TOP_LEVEL(name) \
		auto& __prof = ax::Profiler::instance(); \
		auto __##name = __prof.segment(#name);

#define PROFILER_SEGMENT_SCOPED(parent, sample) \
		auto __##sample = __ ##parent->segment(#sample); \
		ax::Profiler::ScopedSample s(__##sample);

#define PROFILER_SEGMENT_SCOPED_SUB_LEVEL(tl, parent, sample) \
		auto& __prof = ax::Profiler::instance(); \
		auto __##sample = __prof.segment(#tl)->segment(#parent)->segment(#sample); \
		ax::Profiler::ScopedSample __s(__##sample);

#define PROFILER_TOP_LEVEL_SEGMENT_SCOPED(sample) \
		ax::Profiler::ScopedSample __tls(__##sample);

#else

#define PROFILER_TOP_LEVEL(name)

#define PROFILER_SEGMENT_SCOPED(parent, sample)

#define PROFILER_SEGMENT_SCOPED_SUB_LEVEL(tl, parent, sample)

#define PROFILER_TOP_LEVEL_SEGMENT_SCOPED(sample)

#endif