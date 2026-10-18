#include "axeng/core/font_sheet.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace
{
	struct RasterGlyph
	{
		size_t index{};
		bool bold{};
		uint32_t width{};
		uint32_t height{};
		float xOffset{};
		float yOffset{};
		float advance{};
		std::vector<uint8_t> alpha{};
	};

	std::vector<uint8_t> downsample_bitmap(const std::vector<uint8_t>& source, uint32_t width, uint32_t height)
	{
		const uint32_t resultWidth{ (width + 1) / 2 };
		const uint32_t resultHeight{ (height + 1) / 2 };
		std::vector<uint8_t> result(static_cast<size_t>(resultWidth) * resultHeight);

		for (uint32_t y{}; y < resultHeight; ++y)
		{
			for (uint32_t x{}; x < resultWidth; ++x)
			{
				uint32_t sum{};
				uint32_t samples{};
				for (uint32_t dy{}; dy < 2; ++dy)
				{
					for (uint32_t dx{}; dx < 2; ++dx)
					{
						const uint32_t sourceX{ x * 2 + dx };
						const uint32_t sourceY{ y * 2 + dy };
						if (sourceX < width && sourceY < height)
						{
							sum += source[static_cast<size_t>(sourceY) * width + sourceX];
							++samples;
						}
					}
				}
				result[static_cast<size_t>(y) * resultWidth + x] =
					static_cast<uint8_t>((sum + samples / 2) / samples);
			}
		}
		return result;
	}

	std::vector<uint8_t> embolden_bitmap(const std::vector<uint8_t>& source, uint32_t width, uint32_t height)
	{
		const uint32_t resultWidth{ width + 2 };
		const uint32_t resultHeight{ height + 2 };
		std::vector<uint8_t> result(static_cast<size_t>(resultWidth) * resultHeight);
		for (uint32_t y{}; y < height; ++y)
		{
			for (uint32_t x{}; x < width; ++x)
			{
				const uint8_t alpha{ source[static_cast<size_t>(y) * width + x] };
				for (int dy{ -1 }; dy <= 1; ++dy)
				{
					for (int dx{ -1 }; dx <= 1; ++dx)
					{
						const auto destinationX{ static_cast<uint32_t>(static_cast<int>(x) + dx + 1) };
						const auto destinationY{ static_cast<uint32_t>(static_cast<int>(y) + dy + 1) };
						auto& destination{ result[static_cast<size_t>(destinationY) * resultWidth + destinationX] };
						destination = std::max(destination, alpha);
					}
				}
			}
		}
		return result;
	}

	ax::FontGlyphBitmap make_glyph_bitmap(const RasterGlyph& glyph, uint32_t x, uint32_t y)
	{
		return
		{
			static_cast<uint16_t>(x),
			static_cast<uint16_t>(y),
			static_cast<uint16_t>(glyph.width),
			static_cast<uint16_t>(glyph.height),
			glyph.xOffset,
			glyph.yOffset
		};
	}
}

ax::Error ax::generate_source_code_pro_font_sheet(std::span<const uint8_t> fontData, FontSheet& sheet)
{
	sheet = {};
	if (fontData.empty())
		return Error::InvalidTexture;

	std::vector<unsigned char> mutableFontData{ fontData.begin(), fontData.end() };
	stbtt_fontinfo font{};
	if (stbtt_InitFont(&font, mutableFontData.data(), 0) == 0)
		return Error::InvalidTexture;

	constexpr int oversample{ 2 };
	constexpr float fontPixelHeight{ 18.0f };
	const float scale{ stbtt_ScaleForPixelHeight(&font, fontPixelHeight) };
	int ascent{}, descent{}, lineGap{};
	stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
	sheet.baseline = ascent * scale;
	sheet.lineHeight = std::max(fontPixelHeight, (ascent - descent + lineGap) * scale);

	std::vector<RasterGlyph> rasterGlyphs{};
	rasterGlyphs.reserve(font_glyph_count * 2);
	for (size_t index{}; index < font_glyph_count; ++index)
	{
		const int codepoint{ static_cast<int>(font_first_codepoint + index) };
		int glyphWidth{}, glyphHeight{}, xOffset{}, yOffset{};
		unsigned char* bitmap{ stbtt_GetCodepointBitmap(
			&font, scale * oversample, scale * oversample, codepoint,
			&glyphWidth, &glyphHeight, &xOffset, &yOffset) };
		if (glyphWidth < 0 || glyphHeight < 0 || (glyphWidth > 0 && glyphHeight > 0 && bitmap == nullptr))
			return Error::InvalidTexture;

		int advanceWidth{}, leftSideBearing{};
		stbtt_GetCodepointHMetrics(&font, codepoint, &advanceWidth, &leftSideBearing);
		const float advance{ advanceWidth * scale };
		if (glyphWidth > 0 && glyphHeight > 0)
		{
			const auto rawSize{ static_cast<size_t>(glyphWidth) * static_cast<size_t>(glyphHeight) };
			std::vector<uint8_t> rawBitmap{ bitmap, bitmap + rawSize };
			auto boldBitmap{ embolden_bitmap(rawBitmap, static_cast<uint32_t>(glyphWidth), static_cast<uint32_t>(glyphHeight)) };
			const auto boldWidth{ static_cast<uint32_t>(glyphWidth + 2) };
			const auto boldHeight{ static_cast<uint32_t>(glyphHeight + 2) };

			rasterGlyphs.push_back({
				index, false,
				static_cast<uint32_t>((glyphWidth + oversample - 1) / oversample),
				static_cast<uint32_t>((glyphHeight + oversample - 1) / oversample),
				static_cast<float>(xOffset) / oversample,
				static_cast<float>(yOffset) / oversample,
				advance,
				downsample_bitmap(rawBitmap, static_cast<uint32_t>(glyphWidth), static_cast<uint32_t>(glyphHeight))
			});
			rasterGlyphs.push_back({
				index, true,
				(boldWidth + oversample - 1) / oversample,
				(boldHeight + oversample - 1) / oversample,
				static_cast<float>(xOffset - 1) / oversample,
				static_cast<float>(yOffset - 1) / oversample,
				advance,
				downsample_bitmap(boldBitmap, boldWidth, boldHeight)
			});
		}
		else
		{
			rasterGlyphs.push_back({ index, false, 0, 0, 0.0f, 0.0f, advance, {} });
			rasterGlyphs.push_back({ index, true, 0, 0, 0.0f, 0.0f, advance, {} });
		}
		if (bitmap != nullptr)
			stbtt_FreeBitmap(bitmap, nullptr);
	}

	std::stable_sort(rasterGlyphs.begin(), rasterGlyphs.end(), [](const RasterGlyph& lhs, const RasterGlyph& rhs)
	{
		if (lhs.height != rhs.height)
			return lhs.height > rhs.height;
		return lhs.width > rhs.width;
	});

	std::vector<uint8_t> alpha(static_cast<size_t>(font_sheet_width) * font_sheet_height);
	constexpr uint32_t padding{ 2 };
	uint32_t cursorX{ padding };
	uint32_t cursorY{ padding };
	uint32_t rowHeight{};
	for (const auto& glyph : rasterGlyphs)
	{
		auto& metrics{ glyph.bold ? sheet.glyphs[glyph.index].bold : sheet.glyphs[glyph.index].regular };
		if (glyph.width == 0 || glyph.height == 0)
		{
			metrics = make_glyph_bitmap(glyph, 0, 0);
			sheet.glyphs[glyph.index].advance = glyph.advance;
			continue;
		}

		if (cursorX + glyph.width + padding > font_sheet_width)
		{
			cursorX = padding;
			cursorY += rowHeight + padding;
			rowHeight = 0;
		}
		if (cursorY + glyph.height + padding > font_sheet_height)
		{
			sheet = {};
			return Error::InvalidTexture;
		}

		metrics = make_glyph_bitmap(glyph, cursorX, cursorY);
		sheet.glyphs[glyph.index].advance = glyph.advance;
		for (uint32_t y{}; y < glyph.height; ++y)
		{
			for (uint32_t x{}; x < glyph.width; ++x)
			{
				alpha[static_cast<size_t>(cursorY + y) * font_sheet_width + cursorX + x] =
					glyph.alpha[static_cast<size_t>(y) * glyph.width + x];
			}
		}
		cursorX += glyph.width + padding;
		rowHeight = std::max(rowHeight, glyph.height);
	}

	sheet.pixels.resize(alpha.size() * 4);
	for (size_t i{}; i < alpha.size(); ++i)
	{
		const size_t offset{ i * 4 };
		sheet.pixels[offset] = 255;
		sheet.pixels[offset + 1] = 255;
		sheet.pixels[offset + 2] = 255;
		sheet.pixels[offset + 3] = alpha[i];
	}

	return Error::Success;
}

bool ax::is_font_texture_name(std::string_view name) noexcept
{
	constexpr std::string_view token{ "__builtin_font" };
	for (size_t start{}; start + token.size() <= name.size(); ++start)
	{
		bool matches{ true };
		for (size_t i{}; i < token.size(); ++i)
		{
			const auto character{ static_cast<unsigned char>(name[start + i]) };
			if (std::tolower(character) != token[i])
			{
				matches = false;
				break;
			}
		}
		if (matches)
			return true;
	}
	return false;
}
