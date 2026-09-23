#include "perf_profiler.h"

namespace ax
{
	ProfilerSegment::ProfilerSegment()
	{
		m_samples.fill(0);
	}

	void ProfilerSegment::addSample(std::chrono::nanoseconds ns)
	{
		std::lock_guard lock(m_mutex);
		m_samples[m_index] = static_cast<uint64_t>(ns.count());
		m_index = (m_index + 1) % kProfilerSampleCount;
		if (m_count < kProfilerSampleCount) ++m_count;
	}

	PerfStats ProfilerSegment::stats() const
	{
		std::lock_guard lock(m_mutex);
		PerfStats out;
		if (m_count == 0) return out;
		uint64_t sum = 0;
		uint64_t minv = UINT64_MAX;
		uint64_t maxv = 0;
		for (size_t i = 0; i < m_count; ++i)
		{
			uint64_t v = m_samples[i];
			sum += v;
			if (v < minv) minv = v;
			if (v > maxv) maxv = v;
		}
		out.samples = m_count;
		out.averageMs = static_cast<double>(sum) / (1000000.0 * static_cast<double>(m_count));
		out.minMs = static_cast<double>(minv) / 1000000.0;
		out.maxMs = static_cast<double>(maxv) / 1000000.0;
		return out;
	}

	Profiler& Profiler::instance()
	{
		static Profiler s;
		return s;
	}

	std::shared_ptr<ProfilerSegment> Profiler::segment(const std::string& name)
	{
		std::lock_guard lock(m_mutex);
		auto it = m_segments.find(name);
		if (it != m_segments.end()) return it->second;
		auto seg = std::make_shared<ProfilerSegment>();
		m_segments.emplace(name, seg);
		return seg;
	}

	std::vector<std::pair<std::string, std::shared_ptr<ProfilerSegment>>> Profiler::all_segments()
	{
		std::lock_guard lock(m_mutex);
		std::vector<std::pair<std::string, std::shared_ptr<ProfilerSegment>>> out;
		out.reserve(m_segments.size());
		for (auto &kv : m_segments)
			out.emplace_back(kv.first, kv.second);
		return out;
	}

	Profiler::ScopedSample::ScopedSample(std::shared_ptr<ProfilerSegment> seg)
		: m_seg(std::move(seg)), m_start(std::chrono::high_resolution_clock::now())
	{
	}

	Profiler::ScopedSample::~ScopedSample()
	{
		if (m_seg)
		{
			auto end = std::chrono::high_resolution_clock::now();
			m_seg->addSample(std::chrono::duration_cast<std::chrono::nanoseconds>(end - m_start));
		}
	}

}
