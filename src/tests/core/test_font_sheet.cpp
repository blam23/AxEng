#include "axeng/core/font_data.h"
#include "axeng/core/font_sheet.h"

#include <gtest/gtest.h>

TEST(FontSheet, GeneratesRegularAndBoldAsciiGlyphs)
{
	const auto fontData{ ax::source_code_pro_font_data() };
	std::vector<uint8_t> pixels{};

	ASSERT_EQ(ax::generate_source_code_pro_font_sheet(fontData, pixels), ax::Error::Success);
	ASSERT_EQ(pixels.size(), static_cast<size_t>(ax::font_sheet_width) * ax::font_sheet_height * 4);

	const auto countAlpha = [&pixels](uint32_t x, uint32_t y)
	{
		size_t count{};
		for (uint32_t row{}; row < 16; ++row)
		{
			for (uint32_t column{}; column < 16; ++column)
			{
				const size_t offset{ ((static_cast<size_t>(y + row) * ax::font_sheet_width) + x + column) * 4 };
				if (pixels[offset + 3] != 0)
					++count;
			}
		}
		return count;
	};

	EXPECT_EQ(countAlpha(0, 0), 0);
	EXPECT_GT(countAlpha(16, 32), 0);
	EXPECT_GT(countAlpha(16, 160), countAlpha(16, 32));
	EXPECT_GT(countAlpha(240, 16), 0);
}
