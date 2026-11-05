#include "axeng/core/render_profiler.h"

#include <cstring>

void ax::RenderProfiler::initialize(const wgpu::Device& device)
{
	m_device = device;
	m_gpuStatistics.supported = device.HasFeature(wgpu::FeatureName::TimestampQuery);
}

void ax::RenderProfiler::configure(bool counters, bool timestamps)
{
	m_countersEnabled = counters;
	m_timestampsEnabled = timestamps;
}

void ax::RenderProfiler::collect()
{
	for (auto& slot : m_slots)
	{
		if (!slot.completion) continue;
		const auto status{ slot.completion->status.load(std::memory_order_acquire) };
		if (status == 0) continue;
		if (status == 1)
		{
			const auto* bytes{ static_cast<const std::byte*>(slot.readback.GetConstMappedRange()) };
			if (bytes && slot.frame >= m_gpuStatistics.frame)
			{
				m_gpuStatistics.frame = slot.frame;
				m_gpuStatistics.passes.clear();
				m_gpuStatistics.valid = true;
				for (std::size_t i{}; i < slot.names.size(); ++i)
				{
					std::uint64_t begin{}, end{};
					std::memcpy(&begin, bytes + i * 16, sizeof(begin));
					std::memcpy(&end, bytes + i * 16 + 8, sizeof(end));
					if (end < begin) { m_gpuStatistics.valid = false; break; }
					m_gpuStatistics.passes.push_back({ slot.names[i], static_cast<double>(end - begin) / 1e6 });
				}
			}
			slot.readback.Unmap();
		}
		else ++m_gpuStatistics.failedReadbacks;
		slot.completion.reset();
	}
}

void ax::RenderProfiler::begin_frame()
{
	collect();
	const auto frame{ m_statistics.frame + 1 };
	m_statistics = {};
	m_statistics.frame = frame;
	m_statistics.enabled = m_countersEnabled;
	m_activeSlot = nullptr;
	m_gpuStatistics.enabled = m_timestampsEnabled && m_gpuStatistics.supported;
	if (m_gpuStatistics.enabled)
	{
		for (auto& slot : m_slots)
		{
			if (slot.completion) continue;
			if (!slot.queries)
			{
				wgpu::QuerySetDescriptor queryDesc{};
				queryDesc.type = wgpu::QueryType::Timestamp;
				queryDesc.count = s_max_passes * 2;
				slot.queries = m_device.CreateQuerySet(&queryDesc);
				wgpu::BufferDescriptor bufferDesc{};
				bufferDesc.size = s_max_passes * 16;
				bufferDesc.usage = wgpu::BufferUsage::QueryResolve | wgpu::BufferUsage::CopySrc;
				slot.resolve = m_device.CreateBuffer(&bufferDesc);
				bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
				slot.readback = m_device.CreateBuffer(&bufferDesc);
			}
			slot.names.clear();
			slot.frame = frame;
			m_activeSlot = &slot;
			break;
		}
		if (!m_activeSlot) ++m_gpuStatistics.skippedFrames;
	}
	s_current = this;
}

void ax::RenderProfiler::end_frame()
{
	s_current = nullptr;
	if (!m_activeSlot || m_activeSlot->names.empty()) return;
	auto& slot{ *m_activeSlot };
	auto encoder{ m_device.CreateCommandEncoder() };
	encoder.ResolveQuerySet(slot.queries, 0, static_cast<std::uint32_t>(slot.names.size() * 2), slot.resolve, 0);
	encoder.CopyBufferToBuffer(slot.resolve, 0, slot.readback, 0, slot.names.size() * 16);
	auto commands{ encoder.Finish() };
	m_device.GetQueue().Submit(1, &commands);
	slot.completion = std::make_shared<Readback>();
	// Only publish completion here; mapped bytes and GPU objects are accessed on the window thread in collect().
	slot.readback.MapAsync(wgpu::MapMode::Read, 0, s_max_passes * 16, wgpu::CallbackMode::AllowSpontaneous,
		[completion = slot.completion](wgpu::MapAsyncStatus status, wgpu::StringView)
		{
			completion->status.store(status == wgpu::MapAsyncStatus::Success ? 1 : -1, std::memory_order_release);
		});
	m_activeSlot = nullptr;
}

bool ax::RenderProfiler::timestamp_indices(const char* name, std::uint32_t& first)
{
	if (!m_activeSlot) return false;
	if (m_activeSlot->names.size() >= s_max_passes)
	{
		++m_gpuStatistics.droppedPasses;
		return false;
	}
	first = static_cast<std::uint32_t>(m_activeSlot->names.size() * 2);
	m_activeSlot->names.emplace_back(name);
	return true;
}

wgpu::RenderPassEncoder ax::RenderProfiler::begin_render_pass(const wgpu::CommandEncoder& encoder,
	const wgpu::RenderPassDescriptor& descriptor, const char* name)
{
	auto desc{ descriptor };
	wgpu::PassTimestampWrites timestamps{};
	std::uint32_t first{};
	if (!desc.timestampWrites && timestamp_indices(name, first))
	{
		timestamps.querySet = m_activeSlot->queries;
		timestamps.beginningOfPassWriteIndex = first;
		timestamps.endOfPassWriteIndex = first + 1;
		desc.timestampWrites = &timestamps;
	}
	return encoder.BeginRenderPass(&desc);
}

wgpu::ComputePassEncoder ax::RenderProfiler::begin_compute_pass(const wgpu::CommandEncoder& encoder,
	const wgpu::ComputePassDescriptor& descriptor, const char* name)
{
	auto desc{ descriptor };
	wgpu::PassTimestampWrites timestamps{};
	std::uint32_t first{};
	if (!desc.timestampWrites && timestamp_indices(name, first))
	{
		timestamps.querySet = m_activeSlot->queries;
		timestamps.beginningOfPassWriteIndex = first;
		timestamps.endOfPassWriteIndex = first + 1;
		desc.timestampWrites = &timestamps;
	}
	return encoder.BeginComputePass(&desc);
}

void ax::RenderProfiler::record_write(RenderCategory category, std::uint64_t bytes)
{
	if (!m_countersEnabled) return;
	auto& counters{ m_statistics.categories[static_cast<std::size_t>(category)] };
	++counters.writes;
	counters.bytes += bytes;
}

void ax::RenderProfiler::record_draw(RenderCategory category)
{
	if (m_countersEnabled) ++m_statistics.categories[static_cast<std::size_t>(category)].draws;
}

void ax::RenderProfiler::record_packed(RenderCategory category, std::uint64_t count)
{
	if (m_countersEnabled) m_statistics.categories[static_cast<std::size_t>(category)].packed += count;
}

void ax::RenderProfiler::record_tessellation() { if (m_countersEnabled) ++m_statistics.tessellations; }
void ax::RenderProfiler::record_growth() { if (m_countersEnabled) ++m_statistics.bufferGrowths; }
void ax::RenderProfiler::record_submit() { if (m_countersEnabled) ++m_statistics.submits; }
void ax::RenderProfiler::record_cpu(RenderCategory category, double milliseconds)
{
	if (m_countersEnabled) m_statistics.categories[static_cast<std::size_t>(category)].cpuMilliseconds += milliseconds;
}

ax::RenderCpuScope::RenderCpuScope(RenderCategory category)
	: m_category{ category }, m_profiler{ RenderProfiler::current() }
{
	if (m_profiler && m_profiler->statistics().enabled) m_started = std::chrono::steady_clock::now();
	else m_profiler = nullptr;
}

ax::RenderCpuScope::~RenderCpuScope()
{
	if (m_profiler) m_profiler->record_cpu(m_category,
		std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_started).count());
}
