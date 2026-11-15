#include "axeng/debug/gpu_pass_timer.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cstring>

void ax::GpuPassTimer::init(const wgpu::Device& device)
{
	m_querySet = {};
	m_last.clear();
	if (!device || !device.HasFeature(wgpu::FeatureName::TimestampQuery))
		return;
	wgpu::QuerySetDescriptor queryDesc{};
	queryDesc.label = "GPU pass timer";
	queryDesc.type = wgpu::QueryType::Timestamp;
	queryDesc.count = s_max_passes * 2;
	m_querySet = device.CreateQuerySet(&queryDesc);
	wgpu::BufferDescriptor resolveDesc{};
	resolveDesc.label = "GPU pass timer resolve";
	resolveDesc.size = s_max_passes * 2 * sizeof(std::uint64_t);
	resolveDesc.usage = wgpu::BufferUsage::QueryResolve | wgpu::BufferUsage::CopySrc;
	m_resolve = gpu::create_buffer(device, resolveDesc, "Profiling");
	for (auto& slot : m_slots)
	{
		wgpu::BufferDescriptor readDesc{};
		readDesc.label = "GPU pass timer read-back";
		readDesc.size = resolveDesc.size;
		readDesc.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
		slot.buffer = gpu::create_buffer(device, readDesc, "Profiling");
		slot.state = SlotState::free;
	}
}

void ax::GpuPassTimer::begin_frame()
{
	m_categories.clear();
	if (!enabled())
		return;
	for (auto& slot : m_slots)
	{
		if (slot.state == SlotState::submitted)
		{
			// Copied on an earlier frame, since submitted: ask for it.
			slot.state = SlotState::mapping;
			wgpu::Buffer buffer{ slot.buffer };
			buffer.MapAsync(wgpu::MapMode::Read, 0, slot.categories.size() * 2 * sizeof(std::uint64_t),
				wgpu::CallbackMode::AllowSpontaneous,
				[&slot](wgpu::MapAsyncStatus status, wgpu::StringView)
				{
					slot.state = status == wgpu::MapAsyncStatus::Success ? SlotState::ready : SlotState::free;
				});
		}
		else if (slot.state == SlotState::ready)
		{
			wgpu::Buffer buffer{ slot.buffer };
			const auto count{ slot.categories.size() };
			std::vector<std::uint64_t> stamps(count * 2);
			std::memcpy(stamps.data(), buffer.GetConstMappedRange(0, count * 2 * sizeof(std::uint64_t)),
				count * 2 * sizeof(std::uint64_t));
			buffer.Unmap();
			slot.state = SlotState::free;
			m_last.clear();
			for (std::size_t index{}; index < count; ++index)
			{
				// Nanoseconds; a pass the GPU reordered or a disjoint reading gives nonsense, left out.
				const auto begin{ stamps[index * 2] }, end{ stamps[index * 2 + 1] };
				const auto milliseconds{ end > begin ? static_cast<double>(end - begin) * 1e-6 : 0.0 };
				const auto found{ std::ranges::find(m_last, slot.categories[index], &Timing::category) };
				if (found == m_last.end()) m_last.push_back({ slot.categories[index], milliseconds });
				else found->milliseconds += milliseconds;
			}
			double total{};
			for (const auto& timing : m_last)
			{
				total += timing.milliseconds;
				TracyPlotConfig(timing.category, tracy::PlotFormatType::Number, false, true, 0);
				TracyPlot(timing.category, timing.milliseconds);
			}
			TracyPlot("GPU ms: lighting total", total);
		}
	}
}

const wgpu::PassTimestampWrites* ax::GpuPassTimer::pass(const char* category)
{
	if (!enabled() || m_categories.size() >= s_max_passes)
		return nullptr;
	const auto index{ static_cast<std::uint32_t>(m_categories.size()) };
	m_categories.push_back(category);
	m_writes = {};
	m_writes.querySet = m_querySet;
	m_writes.beginningOfPassWriteIndex = index * 2;
	m_writes.endOfPassWriteIndex = index * 2 + 1;
	return &m_writes;
}

void ax::GpuPassTimer::submitted()
{
	for (auto& slot : m_slots)
		if (slot.state == SlotState::recorded) slot.state = SlotState::submitted;
}

void ax::GpuPassTimer::end_frame(wgpu::CommandEncoder& encoder)
{
	if (!enabled() || m_categories.empty())
		return;
	Slot* free{};
	for (auto& slot : m_slots)
		if (!free && slot.state == SlotState::free) free = &slot;
	// Every read-back still in flight (the GPU or the map callbacks running behind): skip this frame's.
	if (!free)
		return;
	const auto count{ static_cast<std::uint32_t>(m_categories.size()) };
	encoder.ResolveQuerySet(m_querySet, 0, count * 2, m_resolve, 0);
	encoder.CopyBufferToBuffer(m_resolve, 0, free->buffer, 0, count * 2 * sizeof(std::uint64_t));
	free->categories = m_categories;
	free->state = SlotState::recorded;
}
