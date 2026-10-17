#include "axeng/core/font_data.h"
#include "axeng/core/font_sheet.h"

#include <gtest/gtest.h>

TEST(FontSheet, GeneratesRegularAndBoldAsciiGlyphs)
{
	const auto fontData{ ax::source_code_pro_font_data() };
	ax::FontSheet sheet{};

	ASSERT_EQ(ax::generate_source_code_pro_font_sheet(fontData, sheet), ax::Error::Success);
	ASSERT_EQ(sheet.pixels.size(), static_cast<size_t>(ax::font_sheet_width) * ax::font_sheet_height * 4);

	const auto countAlpha = [&sheet](const ax::FontGlyphBitmap& bitmap)
	{
		size_t count{};
		for (uint32_t y{}; y < bitmap.height; ++y)
		{
			for (uint32_t x{}; x < bitmap.width; ++x)
			{
				const size_t offset{
					((static_cast<size_t>(bitmap.y + y) * ax::font_sheet_width) + bitmap.x + x) * 4 + 3 };
				if (sheet.pixels[offset] != 0)
					++count;
			}
		}
		return count;
	};

	const auto& space{ sheet.glyphs[' ' - ax::font_first_codepoint] };
	const auto& narrow{ sheet.glyphs['I' - ax::font_first_codepoint] };
	const auto& wide{ sheet.glyphs['W' - ax::font_first_codepoint] };
	EXPECT_EQ(space.regular.width, 0);
	EXPECT_GT(space.advance, 0.0f);
	EXPECT_GT(narrow.regular.width, 0);
	EXPECT_GT(wide.regular.width, narrow.regular.width);
	EXPECT_GT(countAlpha(wide.regular), 0);
	EXPECT_GT(wide.bold.width, wide.regular.width);
	EXPECT_GT(countAlpha(wide.bold), countAlpha(wide.regular));

	for (const auto& glyph : sheet.glyphs)
	{
		for (const auto& bitmap : { glyph.regular, glyph.bold })
		{
			EXPECT_LE(static_cast<uint32_t>(bitmap.x) + bitmap.width, ax::font_sheet_width);
			EXPECT_LE(static_cast<uint32_t>(bitmap.y) + bitmap.height, ax::font_sheet_height);
		}
	}
}
