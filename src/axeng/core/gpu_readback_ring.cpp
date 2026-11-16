#include "gpu_readback_ring.h"

#include <algorithm>
#include <cstring>

ax::GpuReadbackRing::~GpuReadbackRing()
{
	clear();
}

bool ax::GpuReadbackRing::initialise(const wgpu::Device& device, std::uint64_t slotBytes, std::uint32_t slots,
	std::string_view label)
{
	clear();
	if (!device || slotBytes == 0 || slotBytes % 4 != 0 || slots == 0) return false;
	m_label = label.empty() ? std::string{ "Readback ring" } : std::string{ label };
	m_slotBytes = slotBytes;
	m_slots.resize(slots);
	for (std::uint32_t index{}; index < slots; ++index)
	{
		wgpu::BufferDescriptor descriptor{};
		descriptor.size = slotBytes;
		descriptor.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
		const auto name{ m_label + " slot " + std::to_string(index) };
		descriptor.label = { name.data(), name.size() };
		m_slots[index].buffer = gpu::create_buffer(device, descriptor, "Readback");
		m_slots[index].state = std::make_shared<std::atomic<State>>(State::Free);
		if (!m_slots[index].buffer)
		{
			clear();
			return false;
		}
	}
	return true;
}

void ax::GpuReadbackRing::clear() noexcept
{
	for (auto& slot : m_slots)
	{
		// A mapped buffer must be unmapped before it goes; one still mapping is cancelled when it is destroyed.
		if (slot.state && slot.state->load() == State::Mapped && slot.buffer)
			slot.buffer.Unmap();
	}
	m_slots.clear();
	m_slotBytes = 0;
}

bool ax::GpuReadbackRing::encode_copy(wgpu::CommandEncoder& encoder, const wgpu::Buffer& source,
	std::uint64_t sourceOffset, std::uint64_t tag)
{
	if (!source) return false;
	for (auto& slot : m_slots)
	{
		if (slot.state->load() != State::Free) continue;
		encoder.CopyBufferToBuffer(source, sourceOffset, slot.buffer, 0, m_slotBytes);
		slot.tag = tag;
		slot.state->store(State::Recorded);
		return true;
	}
	return false;
}

void ax::GpuReadbackRing::submitted()
{
	for (auto& slot : m_slots)
	{
		if (slot.state->load() != State::Recorded) continue;
		slot.state->store(State::Mapping);
		// Spontaneous: the callback may run inside any later Dawn call or device tick (the window ticks every frame), or
		// from the browser's event loop. It only flips the state, which poll() reads.
		slot.buffer.MapAsync(wgpu::MapMode::Read, 0, m_slotBytes, wgpu::CallbackMode::AllowSpontaneous,
			[state = slot.state](wgpu::MapAsyncStatus status, wgpu::StringView)
			{
				state->store(status == wgpu::MapAsyncStatus::Success ? State::Mapped : State::Failed);
			});
	}
}

std::size_t ax::GpuReadbackRing::poll(const std::function<void(std::uint64_t, std::span<const std::byte>)>& receive)
{
	std::vector<Slot*> waiting{};
	for (auto& slot : m_slots)
	{
		const auto state{ slot.state->load() };
		if (state == State::Mapping || state == State::Mapped || state == State::Failed)
			waiting.push_back(&slot);
	}
	std::ranges::sort(waiting, {}, &Slot::tag);
	std::size_t handed{};
	for (auto* slot : waiting)
	{
		const auto state{ slot->state->load() };
		if (state == State::Mapping) break;
		if (state == State::Mapped)
		{
			const auto* data{ static_cast<const std::byte*>(slot->buffer.GetConstMappedRange(0, m_slotBytes)) };
			if (data && receive) receive(slot->tag, { data, static_cast<std::size_t>(m_slotBytes) });
			slot->buffer.Unmap();
			++handed;
		}
		slot->state->store(State::Free);
	}
	return handed;
}

std::uint32_t ax::GpuReadbackRing::pending() const noexcept
{
	return static_cast<std::uint32_t>(std::ranges::count_if(m_slots, [](const Slot& slot)
		{
			const auto state{ slot.state->load() };
			return state == State::Recorded || state == State::Mapping || state == State::Mapped;
		}));
}
