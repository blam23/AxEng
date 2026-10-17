#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "axeng/core/error.h"

namespace ax
{
	inline constexpr uint32_t font_sheet_width{ 256 };
	inline constexpr uint32_t font_sheet_height{ 256 };
	inline constexpr uint32_t font_first_codepoint{ 32 };
	inline constexpr size_t font_glyph_count{ 95 };

	struct FontGlyphBitmap
	{
		uint16_t x{};
		uint16_t y{};
		uint16_t width{};
		uint16_t height{};
		float xOffset{};
		float yOffset{};
	};

	struct FontGlyph
	{
		FontGlyphBitmap regular{};
		FontGlyphBitmap bold{};
		float advance{};
	};

	struct FontSheet
	{
		std::array<FontGlyph, font_glyph_count> glyphs{};
		std::vector<uint8_t> pixels{};
		float baseline{};
		float lineHeight{};
	};

	Error generate_source_code_pro_font_sheet(std::span<const uint8_t> fontData, FontSheet& sheet);
	bool is_font_texture_name(std::string_view name) noexcept;
}
