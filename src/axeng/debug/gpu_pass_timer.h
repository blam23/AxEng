#pragma once

#include "axeng/debug/gpu_memory.h"

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace ax
{
	// Times render passes on the GPU with timestamp queries (where the device has the TimestampQuery feature), summed per
	// category. Results come back a few frames late, without stalling: each frame's timestamps are resolved and copied
	// into one of a few read-back buffers, mapped on a later frame. They are published as Tracy plots ("GPU ms: <category>")
	// and kept for last().
	class GpuPassTimer
	{
	public:
		struct Timing
		{
			// The category as given to pass(): a string literal (Tracy keeps the pointer as the plot's name).
			const char* category{};
			double milliseconds{};
		};

		// Off (pass() returns null) unless the device has the TimestampQuery feature.
		void init(const wgpu::Device&);
		bool enabled() const noexcept { return m_querySet != nullptr; }

		// Collects results that have come back and starts a frame's list.
		void begin_frame();
		// Timestamp writes for one render pass of a category (a string literal), to set as the pass descriptor's
		// timestampWrites; null when off or this frame's queries are used up. Valid until the next call.
		const wgpu::PassTimestampWrites* pass(const char* category);
		// Resolves this frame's timestamps and copies them for reading back. Call after the frame's last timed pass.
		void end_frame(wgpu::CommandEncoder&);
		// The command buffers end_frame recorded into have been submitted: their read-backs may now be mapped. Without
		// this no results come back (a buffer cannot be mapped before the copy into it is submitted).
		void submitted();

		// The latest frame's times per category, in the order first timed (empty until results come back).
		const std::vector<Timing>& last() const noexcept { return m_last; }

	private:
		static constexpr std::uint32_t s_max_passes{ 64 };
		static constexpr std::uint32_t s_slots{ 4 };
		enum class SlotState { free, recorded, submitted, mapping, ready };
		struct Slot
		{
			gpu::TrackedBuffer buffer{};
			// Set by the map callback, which may run on another thread.
			std::atomic<SlotState> state{ SlotState::free };
			std::vector<const char*> categories{};
		};

		wgpu::QuerySet m_querySet{};
		gpu::TrackedBuffer m_resolve{};
		std::array<Slot, s_slots> m_slots{};
		std::vector<const char*> m_categories{};
		wgpu::PassTimestampWrites m_writes{};
		std::vector<Timing> m_last{};
	};
}
