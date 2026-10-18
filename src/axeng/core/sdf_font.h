#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "axeng/core/error.h"
#include "axeng/core/font_sheet.h"

namespace ax
{
	// Glyphs are rasterised as signed distance fields at this pixel height. Metrics are reported at
	// sdf_font_reference_height so layouts match the bitmap font sheet.
	inline constexpr float sdf_font_pixel_height{ 48.0f };
	inline constexpr float sdf_font_reference_height{ 18.0f };
	// Distance in texels covered by the field on each side of a glyph edge.
	inline constexpr int sdf_font_spread{ 8 };
	inline constexpr uint32_t sdf_font_atlas_width{ 512 };
	inline constexpr std::string_view sdf_font_texture_name{ "__builtin_sdf_font" };

	struct SdfGlyph
	{
		// Texel rectangle in the atlas.
		uint16_t x{};
		uint16_t y{};
		uint16_t width{};
		uint16_t height{};
		// Offset of the rectangle's top-left from the pen position on the baseline, in reference pixels.
		float xOffset{};
		float yOffset{};
		float advance{};
	};

	struct SdfFont
	{
		std::array<SdfGlyph, font_glyph_count> glyphs{};
		// RGBA: white with the distance in alpha (128 = edge, higher = inside).
		std::vector<uint8_t> pixels{};
		uint32_t width{};
		uint32_t height{};
		float baseline{};
		float lineHeight{};
		// Reference pixels per atlas texel; scale glyph sprites by this.
		float texelScale{};
	};

	Error generate_sdf_font(std::span<const uint8_t> fontData, SdfFont& font);
}
