#include "axeng/core/sdf_font.h"

#include <algorithm>

#include <stb_truetype.h>

ax::Error ax::generate_sdf_font(std::span<const uint8_t> fontData, SdfFont& font)
{
	font = {};
	if (fontData.empty())
		return Error::InvalidTexture;

	std::vector<unsigned char> mutableFontData{ fontData.begin(), fontData.end() };
	stbtt_fontinfo info{};
	if (stbtt_InitFont(&info, mutableFontData.data(), 0) == 0)
		return Error::InvalidTexture;

	const float scale{ stbtt_ScaleForPixelHeight(&info, sdf_font_pixel_height) };
	const float referenceScale{ stbtt_ScaleForPixelHeight(&info, sdf_font_reference_height) };
	font.texelScale = sdf_font_reference_height / sdf_font_pixel_height;
	int ascent{}, descent{}, lineGap{};
	stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
	font.baseline = ascent * referenceScale;
	font.lineHeight = std::max(sdf_font_reference_height, (ascent - descent + lineGap) * referenceScale);

	struct Raster
	{
		size_t index{};
		int width{};
		int height{};
		std::vector<uint8_t> distance{};
	};
	std::vector<Raster> rasters{};
	rasters.reserve(font_glyph_count);
	constexpr unsigned char onEdge{ 128 };
	constexpr float distanceScale{ static_cast<float>(onEdge) / sdf_font_spread };
	for (size_t index{}; index < font_glyph_count; ++index)
	{
		const int codepoint{ static_cast<int>(font_first_codepoint + index) };
		int advanceWidth{}, leftSideBearing{};
		stbtt_GetCodepointHMetrics(&info, codepoint, &advanceWidth, &leftSideBearing);
		auto& glyph{ font.glyphs[index] };
		glyph.advance = advanceWidth * referenceScale;

		int width{}, height{}, xOffset{}, yOffset{};
		unsigned char* sdf{ stbtt_GetCodepointSDF(&info, scale, codepoint, sdf_font_spread, onEdge, distanceScale,
			&width, &height, &xOffset, &yOffset) };
		if (sdf == nullptr || width <= 0 || height <= 0)
		{
			if (sdf != nullptr)
				stbtt_FreeSDF(sdf, nullptr);
			continue;
		}
		glyph.xOffset = xOffset * font.texelScale;
		glyph.yOffset = yOffset * font.texelScale;
		rasters.push_back({ index, width, height,
			std::vector<uint8_t>{ sdf, sdf + static_cast<size_t>(width) * static_cast<size_t>(height) } });
		stbtt_FreeSDF(sdf, nullptr);
	}

	std::stable_sort(rasters.begin(), rasters.end(), [](const Raster& lhs, const Raster& rhs)
	{
		return lhs.height > rhs.height;
	});

	// Shelf packing; the field is zero (far outside) in the gaps, so neighbours never bleed.
	constexpr uint32_t padding{ 2 };
	uint32_t cursorX{ padding };
	uint32_t cursorY{ padding };
	uint32_t rowHeight{};
	for (const auto& raster : rasters)
	{
		const auto width{ static_cast<uint32_t>(raster.width) };
		const auto height{ static_cast<uint32_t>(raster.height) };
		if (width + padding * 2 > sdf_font_atlas_width)
			return Error::InvalidTexture;
		if (cursorX + width + padding > sdf_font_atlas_width)
		{
			cursorX = padding;
			cursorY += rowHeight + padding;
			rowHeight = 0;
		}
		auto& glyph{ font.glyphs[raster.index] };
		glyph.x = static_cast<uint16_t>(cursorX);
		glyph.y = static_cast<uint16_t>(cursorY);
		glyph.width = static_cast<uint16_t>(width);
		glyph.height = static_cast<uint16_t>(height);
		cursorX += width + padding;
		rowHeight = std::max(rowHeight, height);
	}
	font.width = sdf_font_atlas_width;
	font.height = (cursorY + rowHeight + padding + 3) & ~3u;

	font.pixels.assign(static_cast<size_t>(font.width) * font.height * 4, 0);
	for (size_t i{}; i < font.pixels.size(); i += 4)
	{
		font.pixels[i] = 255;
		font.pixels[i + 1] = 255;
		font.pixels[i + 2] = 255;
	}
	for (const auto& raster : rasters)
	{
		const auto& glyph{ font.glyphs[raster.index] };
		for (int y{}; y < raster.height; ++y)
		{
			for (int x{}; x < raster.width; ++x)
			{
				const size_t offset{ ((static_cast<size_t>(glyph.y) + y) * font.width + glyph.x + x) * 4 + 3 };
				font.pixels[offset] = raster.distance[static_cast<size_t>(y) * raster.width + x];
			}
		}
	}
	return Error::Success;
}
