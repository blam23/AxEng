#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <webgpu/webgpu_cpp.h>

// Tracks GPU buffers and textures created through gpu::create_buffer / gpu::create_texture, so the debug view and
// self-tests can report what occupies VRAM. Sizes are estimates from the descriptors (drivers may pad), and objects
// not created through these helpers (ImGui backend, swap chain surfaces) are not listed.
namespace ax::gpu
{
	enum class AllocationKind { Buffer, Texture };

	struct Allocation
	{
		std::uint64_t id{};
		AllocationKind kind{};
		// The descriptor label, e.g. "SpriteBatch" or a texture asset name.
		std::string name;
		// The owning system, e.g. "Window", "Particles", "Texture".
		std::string category;
		// "Storage | CopyDst", etc.
		std::string usage;
		// Textures: "1024x512x1 RGBA8Unorm mips=1 samples=1". Buffers: empty.
		std::string details;
		std::uint64_t bytes{};
	};

	struct Snapshot
	{
		std::vector<Allocation> allocations;
		std::uint64_t totalBytes{};
		std::uint64_t bufferBytes{};
		std::uint64_t textureBytes{};
		// Allocations ever made / released, for spotting churn.
		std::uint64_t totalCreated{};
		std::uint64_t totalReleased{};
	};

	// Live allocations, largest first.
	Snapshot snapshot();

	// Writes a human readable report: totals, per category totals and every allocation, largest first.
	// Returns false (and logs) when the file cannot be written.
	bool dump_report(const std::filesystem::path& path, std::string_view heading = {});
	std::string format_report(std::string_view heading = {});

	std::string format_bytes(std::uint64_t bytes);

	namespace detail
	{
		// Unregisters the allocation when the last copy of the owning handle is destroyed.
		struct Token
		{
			explicit Token(std::uint64_t id) : id(id) {}
			~Token();
			Token(const Token&) = delete;
			Token& operator=(const Token&) = delete;
			std::uint64_t id;
		};
	}

	// A wgpu handle that keeps its allocation registered. Copies share the registration; assigning nullptr or an
	// untracked handle drops it.
	template <class Handle>
	class Tracked : public Handle
	{
	public:
		Tracked() = default;
		Tracked(std::nullptr_t) {}
		Tracked(Handle handle, std::shared_ptr<detail::Token> token)
			: Handle(std::move(handle)), m_token(std::move(token)) {}

		Tracked& operator=(std::nullptr_t)
		{
			Handle::operator=(nullptr);
			m_token.reset();
			return *this;
		}

		Tracked& operator=(const Handle& handle)
		{
			Handle::operator=(handle);
			m_token.reset();
			return *this;
		}

	private:
		std::shared_ptr<detail::Token> m_token;
	};

	using TrackedBuffer = Tracked<wgpu::Buffer>;
	using TrackedTexture = Tracked<wgpu::Texture>;

	// The descriptor's label is the displayed name; always set one.
	TrackedBuffer create_buffer(const wgpu::Device& device, const wgpu::BufferDescriptor& descriptor, std::string_view category);
	TrackedTexture create_texture(const wgpu::Device& device, const wgpu::TextureDescriptor& descriptor, std::string_view category);
}
