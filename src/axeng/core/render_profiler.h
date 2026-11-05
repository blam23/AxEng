#pragma once

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ax
{
	enum class RenderCategory : std::uint32_t { Sprites, Pending, Shapes, Lighting, Materials, Particles, Other, Count };
	struct RenderCounters
	{
		std::uint64_t writes{}, bytes{}, draws{}, packed{};
		double cpuMilliseconds{};
	};
	struct RenderStatistics
	{
		std::uint64_t frame{}, submits{}, tessellations{}, bufferGrowths{};
		bool enabled{};
		std::array<RenderCounters, static_cast<std::size_t>(RenderCategory::Count)> categories{};
	};
	struct RenderGpuPass
	{
		std::string name;
		double milliseconds{};
	};
	struct RenderGpuStatistics
	{
		std::uint64_t frame{}, skippedFrames{}, droppedPasses{}, failedReadbacks{};
		bool supported{}, enabled{}, valid{};
		std::vector<RenderGpuPass> passes;
	};

	class RenderProfiler
	{
	public:
		void initialize(const wgpu::Device& device);
		void configure(bool counters, bool timestamps);
		void begin_frame();
		void end_frame();
		const RenderStatistics& statistics() const noexcept { return m_statistics; }
		const RenderGpuStatistics& gpu_statistics() const noexcept { return m_gpuStatistics; }
		static RenderProfiler* current() noexcept { return s_current; }
		void record_write(RenderCategory category, std::uint64_t bytes);
		void record_draw(RenderCategory category);
		void record_packed(RenderCategory category, std::uint64_t count);
		void record_tessellation();
		void record_growth();
		void record_submit();
		void record_cpu(RenderCategory category, double milliseconds);
		wgpu::RenderPassEncoder begin_render_pass(const wgpu::CommandEncoder& encoder,
			const wgpu::RenderPassDescriptor& descriptor, const char* name);
		wgpu::ComputePassEncoder begin_compute_pass(const wgpu::CommandEncoder& encoder,
			const wgpu::ComputePassDescriptor& descriptor, const char* name);

	private:
		static constexpr std::uint32_t s_max_passes{ 64 };
		struct Readback
		{
			std::atomic_int status{ 0 };
		};
		struct Slot
		{
			wgpu::QuerySet queries;
			wgpu::Buffer resolve, readback;
			std::shared_ptr<Readback> completion;
			std::vector<std::string> names;
			std::uint64_t frame{};
		};
		void collect();
		bool timestamp_indices(const char* name, std::uint32_t& first);
		inline static thread_local RenderProfiler* s_current{};
		wgpu::Device m_device;
		std::array<Slot, 3> m_slots{};
		Slot* m_activeSlot{};
		RenderStatistics m_statistics{};
		RenderGpuStatistics m_gpuStatistics{};
		bool m_countersEnabled{ true }, m_timestampsEnabled{};
	};

	class RenderCpuScope
	{
	public:
		explicit RenderCpuScope(RenderCategory category);
		~RenderCpuScope();
	private:
		RenderCategory m_category;
		RenderProfiler* m_profiler;
		std::chrono::steady_clock::time_point m_started{};
	};

	inline void profiled_write_buffer(RenderCategory category, const wgpu::Queue& queue,
		const wgpu::Buffer& buffer, std::uint64_t offset, const void* data, std::size_t size)
	{
		if (auto* profiler{ RenderProfiler::current() }) profiler->record_write(category, size);
		queue.WriteBuffer(buffer, offset, data, size);
	}
	template<typename... Args>
	void profiled_draw(RenderCategory category, const wgpu::RenderPassEncoder& pass, Args... args)
	{
		if (auto* profiler{ RenderProfiler::current() }) profiler->record_draw(category);
		pass.Draw(args...);
	}
	template<typename... Args>
	void profiled_draw_indexed(RenderCategory category, const wgpu::RenderPassEncoder& pass, Args... args)
	{
		if (auto* profiler{ RenderProfiler::current() }) profiler->record_draw(category);
		pass.DrawIndexed(args...);
	}
	inline void profiled_draw_indirect(RenderCategory category, const wgpu::RenderPassEncoder& pass,
		const wgpu::Buffer& buffer, std::uint64_t offset)
	{
		if (auto* profiler{ RenderProfiler::current() }) profiler->record_draw(category);
		pass.DrawIndirect(buffer, offset);
	}
	inline wgpu::RenderPassEncoder profiled_render_pass(const wgpu::CommandEncoder& encoder,
		const wgpu::RenderPassDescriptor& descriptor, const char* name)
	{
		if (auto* profiler{ RenderProfiler::current() }) return profiler->begin_render_pass(encoder, descriptor, name);
		return encoder.BeginRenderPass(&descriptor);
	}
	inline wgpu::ComputePassEncoder profiled_compute_pass(const wgpu::CommandEncoder& encoder,
		const wgpu::ComputePassDescriptor& descriptor, const char* name)
	{
		if (auto* profiler{ RenderProfiler::current() }) return profiler->begin_compute_pass(encoder, descriptor, name);
		return encoder.BeginComputePass(&descriptor);
	}
}
