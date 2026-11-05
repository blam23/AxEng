#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include <webgpu/webgpu_cpp.h>

#include "axeng/core/texture.h"

namespace ax
{
	// An index list entry names an instance record (low bits) and which of a draw's bound textures the sprite samples.
	inline constexpr std::uint32_t sprite_index_slot_shift{ 29u };
	inline constexpr std::uint32_t sprite_index_record_mask{ (1u << sprite_index_slot_shift) - 1u };
	inline constexpr std::uint32_t no_sprite_record{ ~0u };

	// The authored fields a retained sprite's packed record was built from. Comparing these bit for bit detects any
	// edit, including ones made by writing the fields directly.
	struct SpriteShadow
	{
		SpriteData data{};
		SpriteEffect effect{};
		const Texture* tex{ nullptr };
		bool msdf{ false };

		bool matches(const SpriteDefinition& sprite) const noexcept
		{
			return tex == sprite.tex && msdf == (sprite.tex != nullptr && sprite.tex->msdf())
				&& std::memcmp(&data, &sprite.gpuData, sizeof(SpriteData)) == 0
				&& std::memcmp(&effect, &sprite.effect, sizeof(SpriteEffect)) == 0;
		}
		void assign(const SpriteDefinition& sprite) noexcept
		{
			data = sprite.gpuData;
			effect = sprite.effect;
			tex = sprite.tex;
			msdf = sprite.tex != nullptr && sprite.tex->msdf();
		}
	};

	// Instance records of one sprite format. Retained sprites own stable slots in `records`, a CPU mirror of the GPU
	// buffer's persistent part; only changed slots are uploaded. Frame-queued sprites pack into `transient`, which is
	// uploaded after the persistent part each frame. Draws read records through the visible-index list.
	template<class Record>
	struct SpriteInstanceStore
	{
		std::vector<Record> records{};
		std::vector<SpriteShadow> shadows{};
		std::vector<std::uint8_t> dirtyFlags{};
		std::uint32_t dirtyCount{};
		std::vector<std::uint32_t> freeSlots{};
		std::vector<Record> transient{};
		std::vector<std::uint32_t> indices{};
		wgpu::Buffer instances{};
		wgpu::Buffer indexBuffer{};
		std::uint32_t capacity{ 1024 };
		std::uint32_t indexCapacity{ 1024 };
		// The GPU buffer was replaced, so every persistent record must be written again.
		bool allDirty{ true };

		std::uint32_t acquire()
		{
			std::uint32_t slot;
			if (!freeSlots.empty())
			{
				slot = freeSlots.back();
				freeSlots.pop_back();
			}
			else
			{
				slot = static_cast<std::uint32_t>(records.size());
				records.emplace_back();
				shadows.emplace_back();
				dirtyFlags.push_back(0);
			}
			return slot;
		}
		void release(std::uint32_t slot) { freeSlots.push_back(slot); }
		void mark_dirty(std::uint32_t slot)
		{
			dirtyCount += dirtyFlags[slot] == 0;
			dirtyFlags[slot] = 1;
		}
		std::uint32_t persistent_count() const noexcept { return static_cast<std::uint32_t>(records.size()); }
	};

	// One layer's visible indices for the frame, per format. The first entries of each list are transient records,
	// stored relative to the transient range because the persistent count is not final until the frame is packed.
	struct SpriteLayerIndices
	{
		std::vector<std::uint32_t> ordinary{};
		std::vector<std::uint32_t> sdf{};
		std::uint32_t ordinaryTransient{};
		std::uint32_t sdfTransient{};

		void clear() noexcept
		{
			ordinary.clear();
			sdf.clear();
			ordinaryTransient = sdfTransient = 0;
		}
	};
}
