#include "axeng/debug/gpu_memory.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>

#include <spdlog/spdlog.h>

#include "axeng/core/webgpu_print.h"

namespace
{
	std::mutex s_mutex;
	std::unordered_map<std::uint64_t, ax::gpu::Allocation> s_live;
	std::uint64_t s_nextId{ 1 };
	std::uint64_t s_created{ 0 };
	std::uint64_t s_released{ 0 };

	template <class T>
	std::string to_text(const T& value)
	{
		std::ostringstream stream;
		stream << value;
		return stream.str();
	}

	// Bytes per texel (or per compressed block) for the formats the engine uses; other formats assume 4.
	std::uint64_t bytes_per_texel(wgpu::TextureFormat format)
	{
		using F = wgpu::TextureFormat;
		switch (format)
		{
		case F::R8Unorm: case F::R8Snorm: case F::R8Uint: case F::R8Sint: case F::Stencil8:
			return 1;
		case F::R16Uint: case F::R16Sint: case F::R16Float: case F::RG8Unorm: case F::RG8Snorm: case F::RG8Uint:
		case F::RG8Sint: case F::Depth16Unorm:
			return 2;
		case F::RGBA16Uint: case F::RGBA16Sint: case F::RGBA16Float: case F::RG32Uint: case F::RG32Sint:
		case F::RG32Float:
			return 8;
		case F::RGBA32Uint: case F::RGBA32Sint: case F::RGBA32Float:
			return 16;
		case F::Depth24PlusStencil8: case F::Depth32Float: case F::Depth24Plus:
			return 4;
		case F::Depth32FloatStencil8:
			return 8;
		default:
			return 4;
		}
	}

	void unregister_allocation(std::uint64_t id)
	{
		std::lock_guard lock{ s_mutex };
		if (s_live.erase(id) != 0)
			++s_released;
	}

	std::shared_ptr<ax::gpu::detail::Token> register_allocation(ax::gpu::Allocation allocation)
	{
		std::lock_guard lock{ s_mutex };
		allocation.id = s_nextId++;
		const auto id{ allocation.id };
		s_live.emplace(id, std::move(allocation));
		++s_created;
		return std::make_shared<ax::gpu::detail::Token>(id);
	}

	std::string label_or_default(const wgpu::StringView& label, const char* fallback)
	{
		if (label.data == nullptr || label.length == 0)
			return fallback;
		return label.length == WGPU_STRLEN ? std::string(label.data) : std::string(label.data, label.length);
	}
}

ax::gpu::detail::Token::~Token()
{
	unregister_allocation(id);
}

std::string ax::gpu::format_bytes(std::uint64_t bytes)
{
	constexpr const char* units[]{ "B", "KiB", "MiB", "GiB" };
	auto value{ static_cast<double>(bytes) };
	std::size_t unit{ 0 };
	while (value >= 1024.0 && unit + 1 < std::size(units))
	{
		value /= 1024.0;
		++unit;
	}
	char text[48];
	if (unit == 0)
		std::snprintf(text, sizeof(text), "%llu B", static_cast<unsigned long long>(bytes));
	else
		std::snprintf(text, sizeof(text), "%.2f %s", value, units[unit]);
	return text;
}

ax::gpu::Snapshot ax::gpu::snapshot()
{
	Snapshot result;
	{
		std::lock_guard lock{ s_mutex };
		result.allocations.reserve(s_live.size());
		for (const auto& [id, allocation] : s_live)
			result.allocations.push_back(allocation);
		result.totalCreated = s_created;
		result.totalReleased = s_released;
	}
	std::sort
	(
		result.allocations.begin(), result.allocations.end(),
		[](const Allocation& a, const Allocation& b) { return a.bytes != b.bytes ? a.bytes > b.bytes : a.id < b.id; }
	);
	for (const auto& allocation : result.allocations)
	{
		result.totalBytes += allocation.bytes;
		(allocation.kind == AllocationKind::Buffer ? result.bufferBytes : result.textureBytes) += allocation.bytes;
	}
	return result;
}

std::string ax::gpu::format_report(std::string_view heading)
{
	const auto data{ snapshot() };
	std::ostringstream out;
	out << "GPU memory report";
	if (!heading.empty())
		out << " - " << heading;
	out << "\n(sizes are estimates from descriptors; ImGui backend buffers and swap chain images are not tracked)\n\n";
	out << "Total:    " << format_bytes(data.totalBytes) << " in " << data.allocations.size() << " allocations\n";
	out << "Buffers:  " << format_bytes(data.bufferBytes) << "\n";
	out << "Textures: " << format_bytes(data.textureBytes) << "\n";
	out << "Created: " << data.totalCreated << ", released: " << data.totalReleased << "\n\n";

	struct Group { std::uint64_t bytes{}; std::size_t count{}; };
	std::map<std::string, Group> categories;
	for (const auto& allocation : data.allocations)
	{
		auto& group{ categories[allocation.category] };
		group.bytes += allocation.bytes;
		++group.count;
	}
	std::vector<std::pair<std::string, Group>> sorted{ categories.begin(), categories.end() };
	std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second.bytes > b.second.bytes; });
	out << "By category:\n";
	for (const auto& [name, group] : sorted)
		out << "  " << name << ": " << format_bytes(group.bytes) << " (" << group.count << ")\n";

	out << "\nAllocations (largest first):\n";
	for (const auto& allocation : data.allocations)
	{
		out << "  " << format_bytes(allocation.bytes) << "\t"
			<< (allocation.kind == AllocationKind::Buffer ? "Buffer " : "Texture") << "\t"
			<< allocation.category << "\t" << allocation.name << "\t" << allocation.usage;
		if (!allocation.details.empty())
			out << "\t" << allocation.details;
		out << "\n";
	}
	return out.str();
}

bool ax::gpu::dump_report(const std::filesystem::path& path, std::string_view heading)
{
	std::ofstream file{ path, std::ios::trunc };
	if (!file)
	{
		spdlog::error("<GpuMemory> Cannot write report to '{}'.", path.string());
		return false;
	}
	file << format_report(heading);
	return static_cast<bool>(file);
}

ax::gpu::TrackedBuffer ax::gpu::create_buffer(const wgpu::Device& device, const wgpu::BufferDescriptor& descriptor, std::string_view category)
{
	wgpu::Buffer buffer{ device.CreateBuffer(&descriptor) };
	if (buffer.Get() == nullptr)
		return {};

	Allocation allocation;
	allocation.kind = AllocationKind::Buffer;
	allocation.name = label_or_default(descriptor.label, "<unnamed buffer>");
	allocation.category = category;
	allocation.usage = to_text(descriptor.usage);
	allocation.bytes = descriptor.size;
	return { std::move(buffer), register_allocation(std::move(allocation)) };
}

ax::gpu::TrackedTexture ax::gpu::create_texture(const wgpu::Device& device, const wgpu::TextureDescriptor& descriptor, std::string_view category)
{
	wgpu::Texture texture{ device.CreateTexture(&descriptor) };
	if (texture.Get() == nullptr)
		return {};

	const auto mips{ std::max(descriptor.mipLevelCount, 1u) };
	const auto samples{ std::max(descriptor.sampleCount, 1u) };
	std::uint64_t texels{ 0 };
	for (std::uint32_t level{ 0 }; level < mips; ++level)
	{
		const auto w{ std::max(descriptor.size.width >> level, 1u) };
		const auto h{ std::max(descriptor.size.height >> level, 1u) };
		texels += static_cast<std::uint64_t>(w) * h * descriptor.size.depthOrArrayLayers;
	}

	Allocation allocation;
	allocation.kind = AllocationKind::Texture;
	allocation.name = label_or_default(descriptor.label, "<unnamed texture>");
	allocation.category = category;
	allocation.usage = to_text(descriptor.usage);
	allocation.bytes = texels * bytes_per_texel(descriptor.format) * samples;
	allocation.details = std::to_string(descriptor.size.width) + "x" + std::to_string(descriptor.size.height) + "x"
		+ std::to_string(descriptor.size.depthOrArrayLayers) + " " + to_text(descriptor.format)
		+ " mips=" + std::to_string(mips) + " samples=" + std::to_string(samples);
	return { std::move(texture), register_allocation(std::move(allocation)) };
}
