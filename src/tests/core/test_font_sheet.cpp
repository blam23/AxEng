#include "axeng/core/font_data.h"
#include "axeng/core/font_sheet.h"
#include "axeng/core/msdf_font.h"
#include "axeng/core/sdf_font.h"

#include <gtest/gtest.h>

TEST(SdfFont, GeneratesDistanceFieldGlyphsWithBitmapFontMetrics)
{
	const auto fontData{ ax::main_font_data() };
	ax::SdfFont font{};
	ax::FontSheet sheet{};
	ASSERT_EQ(ax::generate_sdf_font(fontData, font), ax::Error::Success);
	ax::SdfFont highResolutionFont{};
	ASSERT_EQ(ax::generate_sdf_font(fontData, highResolutionFont, 64.0f, 11), ax::Error::Success);
	EXPECT_FLOAT_EQ(highResolutionFont.texelScale, ax::sdf_font_reference_height / 64.0f);
	EXPECT_FLOAT_EQ(highResolutionFont.glyphs['A' - ax::font_first_codepoint].advance,
		font.glyphs['A' - ax::font_first_codepoint].advance);
	ax::SdfFont thickenedFont{};
	ASSERT_EQ(ax::generate_sdf_font(fontData, thickenedFont, ax::sdf_font_pixel_height,
		ax::sdf_font_spread, 0.5f), ax::Error::Success);
	EXPECT_EQ(ax::generate_sdf_font(fontData, highResolutionFont, 0.0f, 8), ax::Error::InvalidTexture);
	EXPECT_EQ(ax::generate_sdf_font(fontData, highResolutionFont, 48.0f, 0), ax::Error::InvalidTexture);
	EXPECT_EQ(ax::generate_sdf_font(fontData, highResolutionFont, 48.0f, 8, -0.1f), ax::Error::InvalidTexture);
	ASSERT_EQ(ax::generate_main_font_sheet(fontData, sheet), ax::Error::Success);
	ASSERT_EQ(font.pixels.size(), static_cast<size_t>(font.width) * font.height * 4);
	EXPECT_FLOAT_EQ(font.texelScale, ax::sdf_font_reference_height / ax::sdf_font_pixel_height);
	EXPECT_NEAR(font.baseline, sheet.baseline, 1e-3f);
	EXPECT_NEAR(font.lineHeight, sheet.lineHeight, 1e-3f);

	const auto distance = [&font](uint32_t x, uint32_t y)
	{
		return font.pixels[(static_cast<size_t>(y) * font.width + x) * 4 + 3];
	};
	for (size_t i{}; i < ax::font_glyph_count; ++i)
	{
		const auto& glyph{ font.glyphs[i] };
		EXPECT_NEAR(glyph.advance, sheet.glyphs[i].advance, 1e-3f);
		EXPECT_LE(static_cast<uint32_t>(glyph.x) + glyph.width, font.width);
		EXPECT_LE(static_cast<uint32_t>(glyph.y) + glyph.height, font.height);
	}

	EXPECT_EQ(font.glyphs[' ' - ax::font_first_codepoint].width, 0);
	// The stem of an "I" is inside (above the 128 edge value); the padded corner is outside.
	const auto& bar{ font.glyphs['I' - ax::font_first_codepoint] };
	ASSERT_GT(bar.width, 2 * ax::sdf_font_spread);
	EXPECT_GT(distance(bar.x + bar.width / 2, bar.y + bar.height / 2), 128);
	EXPECT_LT(distance(bar.x, bar.y), 128);
	const auto& thickenedBar{ thickenedFont.glyphs['I' - ax::font_first_codepoint] };
	bool changedDistance{};
	for (uint32_t y{}; y < bar.height; ++y)
	{
		for (uint32_t x{}; x < bar.width; ++x)
		{
			const auto original{ distance(bar.x + x, bar.y + y) };
			const auto thickened{ thickenedFont.pixels[
				(static_cast<size_t>(thickenedBar.y + y) * thickenedFont.width + thickenedBar.x + x) * 4 + 3] };
			EXPECT_GE(thickened, original);
			changedDistance = changedDistance || thickened > original;
		}
	}
	EXPECT_TRUE(changedDistance);
	// Distances fall off smoothly rather than being a hard mask.
	uint32_t partial{};
	for (uint32_t x{}; x < bar.width; ++x)
	{
		const auto d{ distance(bar.x + x, bar.y + bar.height / 2) };
		if (d > 0 && d < 255)
			++partial;
	}
	EXPECT_GT(partial, 4u);
}

TEST(MsdfFont, GeneratesDistanceFieldGlyphsWithBitmapFontMetrics)
{
	const auto fontData{ ax::main_font_data() };
	ax::SdfFont font{};
	ax::FontSheet sheet{};
	ASSERT_EQ(ax::generate_msdf_font(fontData, font), ax::Error::Success);
	ASSERT_EQ(ax::generate_main_font_sheet(fontData, sheet), ax::Error::Success);
	EXPECT_EQ(ax::MsdfFontSettings{}.edgeColoring, ax::MsdfEdgeColoring::ByDistance);
	EXPECT_EQ(font.pixels.size(), static_cast<size_t>(font.width) * font.height * 4);
	EXPECT_FLOAT_EQ(font.texelScale, ax::sdf_font_reference_height / ax::msdf_font_pixel_height);
	EXPECT_NEAR(font.baseline, sheet.baseline, 1e-3f);
	EXPECT_NEAR(font.lineHeight, sheet.lineHeight, 1e-3f);

	for (size_t i{}; i < ax::font_glyph_count; ++i)
	{
		const auto& glyph{ font.glyphs[i] };
		EXPECT_NEAR(glyph.advance, sheet.glyphs[i].advance, 1e-3f);
		EXPECT_LE(static_cast<uint32_t>(glyph.x) + glyph.width, font.width);
		EXPECT_LE(static_cast<uint32_t>(glyph.y) + glyph.height, font.height);
	}

	const auto& stem{ font.glyphs['I' - ax::font_first_codepoint] };
	ASSERT_GT(stem.width, 2u * static_cast<uint32_t>(ax::msdf_font_spread));
	const auto sample = [&font](uint32_t x, uint32_t y, size_t channel)
	{
		return font.pixels[(static_cast<size_t>(y) * font.width + x) * 4 + channel];
	};
	for (size_t channel{}; channel < 3; ++channel)
	{
		EXPECT_GT(sample(stem.x + stem.width / 2, stem.y + stem.height / 2, channel), 128);
		EXPECT_LT(sample(stem.x, stem.y, channel), 128);
	}
	EXPECT_EQ(sample(stem.x + stem.width / 2, stem.y + stem.height / 2, 3), 255);

	auto invalidSettings{ ax::MsdfFontSettings{} };
	invalidSettings.spread = 0;
	EXPECT_EQ(ax::generate_msdf_font(fontData, font, invalidSettings), ax::Error::InvalidTexture);
}

TEST(FontSheet, GeneratesRegularAndBoldAsciiGlyphs)
{
	const auto fontData{ ax::main_font_data() };
	ax::FontSheet sheet{};

	ASSERT_EQ(ax::generate_main_font_sheet(fontData, sheet), ax::Error::Success);
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
