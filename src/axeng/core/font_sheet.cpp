#include "axeng/core/font_sheet.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

ax::Error ax::generate_source_code_pro_font_sheet(std::span<const uint8_t> fontData, std::vector<uint8_t>& pixels)
{
	pixels.clear();
	if (fontData.empty())
		return Error::InvalidTexture;

	std::vector<unsigned char> mutableFontData{ fontData.begin(), fontData.end() };
	stbtt_fontinfo font{};
	if (stbtt_InitFont(&font, mutableFontData.data(), 0) == 0)
		return Error::InvalidTexture;

	constexpr int cellSize{ 16 };
	constexpr int columns{ static_cast<int>(font_sheet_width) / cellSize };
	constexpr int boldRowOffset{ 8 };
	constexpr float fontPixelHeight{ 14.0f };
	constexpr int baseline{ 13 };
	const float scale{ stbtt_ScaleForPixelHeight(&font, fontPixelHeight) };
	pixels.resize(static_cast<size_t>(font_sheet_width) * font_sheet_height * 4, 0);

	const auto writePixel = [&pixels](int x, int y, uint8_t alpha)
	{
		if (x < 0 || x >= static_cast<int>(font_sheet_width)
			|| y < 0 || y >= static_cast<int>(font_sheet_height) || alpha == 0)
			return;

		const size_t offset{ (static_cast<size_t>(y) * font_sheet_width + static_cast<size_t>(x)) * 4 };
		pixels[offset] = 255;
		pixels[offset + 1] = 255;
		pixels[offset + 2] = 255;
		pixels[offset + 3] = std::max(pixels[offset + 3], alpha);
	};

	for (int codepoint{ 32 }; codepoint <= 126; ++codepoint)
	{
		int glyphWidth{}, glyphHeight{}, xOffset{}, yOffset{};
		unsigned char* bitmap{ stbtt_GetCodepointBitmap(
			&font, scale, scale, codepoint, &glyphWidth, &glyphHeight, &xOffset, &yOffset) };

		const int index{ codepoint - 32 };
		const int column{ index % columns };
		const int row{ index / columns };
		const int cellX{ column * cellSize };
		const int regularCellY{ row * cellSize };
		const int boldCellY{ (row + boldRowOffset) * cellSize };
		int advanceWidth{};
		[[maybe_unused]] int leftSideBearing{};
		stbtt_GetCodepointHMetrics(&font, codepoint, &advanceWidth, &leftSideBearing);
		const int advance{ static_cast<int>(std::round(advanceWidth * scale)) };
		const int glyphX{ cellX + (cellSize - advance) / 2 + xOffset };
		const int glyphY{ regularCellY + baseline + yOffset };

		if (bitmap != nullptr)
		{
			for (int y{}; y < glyphHeight; ++y)
			{
				for (int x{}; x < glyphWidth; ++x)
				{
					const uint8_t alpha{ bitmap[y * glyphWidth + x] };
					writePixel(glyphX + x, glyphY + y, alpha);
					for (int boldOffset{ -1 }; boldOffset <= 1; ++boldOffset)
						writePixel(glyphX + x + boldOffset, boldCellY + baseline + yOffset + y, alpha);
				}
			}
			stbtt_FreeBitmap(bitmap, nullptr);
		}
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
