#pragma once

#include "axeng/debug/gpu_memory.h"

#include <webgpu/webgpu_cpp.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ax
{
	// Brings small pieces of GPU data back to the CPU without ever waiting for them. Each copy goes into its own slot of
	// a ring of mappable buffers; the slot is mapped once the GPU has finished, and poll() hands it over when the data has
	// arrived, a frame or more later. Use it for counters and event records a simulation writes on the GPU; keep bulk
	// data on the GPU. Works the same on native and the web (where mapping is always asynchronous). Window thread only.
	class GpuReadbackRing
	{
	public:
		GpuReadbackRing() = default;
		~GpuReadbackRing();
		GpuReadbackRing(const GpuReadbackRing&) = delete;
		GpuReadbackRing& operator=(const GpuReadbackRing&) = delete;

		// slotBytes must be a multiple of 4. More slots allow more copies in flight before encode_copy refuses.
		bool initialise(const wgpu::Device& device, std::uint64_t slotBytes, std::uint32_t slots, std::string_view label = {});
		void clear() noexcept;
		bool ready() const noexcept { return !m_slots.empty(); }
		std::uint64_t slot_bytes() const noexcept { return m_slotBytes; }

		// Records a copy of slot_bytes() bytes at sourceOffset of source (which needs BufferUsage::CopySrc) into a free
		// slot, labelled with tag (a frame or tick number). Returns false, recording nothing, when no slot is free.
		bool encode_copy(wgpu::CommandEncoder& encoder, const wgpu::Buffer& source, std::uint64_t sourceOffset,
			std::uint64_t tag);
		// Call once the command buffers recorded with encode_copy have been submitted: starts mapping their slots.
		void submitted();
		// Hands every slot whose data has arrived to receive, in tag order, and frees it. A slot still mapping holds back
		// the slots with later tags, so data always arrives in order. Never waits. Returns how many slots it handed over.
		std::size_t poll(const std::function<void(std::uint64_t tag, std::span<const std::byte> data)>& receive);
		// Slots recorded or mapping, not yet handed over.
		std::uint32_t pending() const noexcept;

	private:
		enum class State : std::uint8_t { Free, Recorded, Mapping, Mapped, Failed };
		struct Slot
		{
			gpu::TrackedBuffer buffer{};
			std::uint64_t tag{};
			// Shared with the map callback, which can outlive the ring (a slot cleared while it maps).
			std::shared_ptr<std::atomic<State>> state{};
		};
		std::vector<Slot> m_slots{};
		std::uint64_t m_slotBytes{};
		std::string m_label{};
	};
}
