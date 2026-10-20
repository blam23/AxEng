#include "axeng/core/msdf_font.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#include <msdfgen.h>
#include <msdfgen-ext.h>

ax::Error ax::generate_msdf_font(std::span<const uint8_t> fontData, SdfFont& font,
	const MsdfFontSettings& settings)
{
	font = {};
	if (fontData.empty() || fontData.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
		!std::isfinite(settings.pixelHeight) || settings.pixelHeight <= 0.0f || settings.spread <= 0 ||
		!std::isfinite(settings.thickness) || settings.thickness < 0.0f ||
		settings.thickness > settings.spread * sdf_font_reference_height / settings.pixelHeight ||
		!std::isfinite(settings.edgeAngleThreshold) || settings.edgeAngleThreshold < 0.0)
		return Error::InvalidTexture;

	using FreetypeHandle = std::unique_ptr<msdfgen::FreetypeHandle,
		decltype(&msdfgen::deinitializeFreetype)>;
	using FontHandle = std::unique_ptr<msdfgen::FontHandle, decltype(&msdfgen::destroyFont)>;
	FreetypeHandle freetype{ msdfgen::initializeFreetype(), &msdfgen::deinitializeFreetype };
	if (!freetype)
		return Error::GenericFailure;
	FontHandle msdfFont{ msdfgen::loadFontData(freetype.get(),
		reinterpret_cast<const msdfgen::byte*>(fontData.data()), static_cast<int>(fontData.size())),
		&msdfgen::destroyFont };
	if (!msdfFont)
		return Error::InvalidTexture;

	msdfgen::FontMetrics metrics{};
	if (!msdfgen::getFontMetrics(metrics, msdfFont.get(), msdfgen::FONT_SCALING_EM_NORMALIZED))
		return Error::GenericFailure;
	const double metricHeight{ metrics.ascenderY - metrics.descenderY };
	if (!std::isfinite(metricHeight) || metricHeight <= 0.0)
		return Error::InvalidTexture;
	const double pixelScale{ settings.pixelHeight / metricHeight };
	const double referenceScale{ sdf_font_reference_height / metricHeight };
	font.texelScale = sdf_font_reference_height / settings.pixelHeight;
	font.baseline = static_cast<float>(metrics.ascenderY * referenceScale);
	font.lineHeight = static_cast<float>(std::max<double>(sdf_font_reference_height,
		metrics.lineHeight * referenceScale));

	struct Raster
	{
		std::size_t index{};
		int width{};
		int height{};
		std::vector<std::uint8_t> rgba{};
	};
	std::vector<Raster> rasters{};
	rasters.reserve(font_glyph_count);
	const double distanceRange{ 2.0 * settings.spread / pixelScale };
	const double encodedThickness{ settings.thickness / referenceScale / distanceRange };
	for (std::size_t index{}; index < font_glyph_count; ++index)
	{
		const auto codepoint{ static_cast<msdfgen::unicode_t>(font_first_codepoint + index) };
		msdfgen::Shape shape{};
		double advance{};
		if (!msdfgen::loadGlyph(shape, msdfFont.get(), codepoint,
			msdfgen::FONT_SCALING_EM_NORMALIZED, &advance))
			return Error::GenericFailure;
		font.glyphs[index].advance = static_cast<float>(advance * referenceScale);
		if (shape.edgeCount() == 0)
			continue;

		shape.normalize();
		if (!shape.validate())
			return Error::GenericFailure;
		shape.orientContours();
		switch (settings.edgeColoring)
		{
		case MsdfEdgeColoring::Simple:
			msdfgen::edgeColoringSimple(shape, settings.edgeAngleThreshold);
			break;
		case MsdfEdgeColoring::ByDistance:
			msdfgen::edgeColoringByDistance(shape, settings.edgeAngleThreshold);
			break;
		default:
			return Error::InvalidTexture;
		}
		const auto bounds{ shape.getBounds() };
		const int left{ static_cast<int>(std::floor(bounds.l * pixelScale)) - settings.spread };
		const int right{ static_cast<int>(std::ceil(bounds.r * pixelScale)) + settings.spread };
		const int bottom{ static_cast<int>(std::floor(bounds.b * pixelScale)) - settings.spread };
		const int top{ static_cast<int>(std::ceil(bounds.t * pixelScale)) + settings.spread };
		const int glyphWidth{ right - left };
		const int glyphHeight{ top - bottom };
		if (glyphWidth <= 0 || glyphHeight <= 0 ||
			glyphWidth > std::numeric_limits<std::uint16_t>::max() ||
			glyphHeight > std::numeric_limits<std::uint16_t>::max())
			return Error::InvalidTexture;

		auto& glyph{ font.glyphs[index] };
		glyph.xOffset = static_cast<float>(left * font.texelScale);
		glyph.yOffset = static_cast<float>(-top * font.texelScale);
		msdfgen::Bitmap<float, 3> bitmap{ glyphWidth, glyphHeight, msdfgen::Y_DOWNWARD };
		const msdfgen::Projection projection{
			msdfgen::Vector2(pixelScale, -pixelScale),
			msdfgen::Vector2(-left / pixelScale, -top / pixelScale) };
		const msdfgen::SDFTransformation transformation{ projection, msdfgen::Range(distanceRange) };
		msdfgen::MSDFGeneratorConfig generatorConfig{};
		switch (settings.errorCorrection)
		{
		case MsdfErrorCorrection::Disabled:
			generatorConfig.errorCorrection.mode = msdfgen::ErrorCorrectionConfig::DISABLED;
			break;
		case MsdfErrorCorrection::EdgePriority:
			generatorConfig.errorCorrection.mode = msdfgen::ErrorCorrectionConfig::EDGE_PRIORITY;
			break;
		case MsdfErrorCorrection::EdgeOnly:
			generatorConfig.errorCorrection.mode = msdfgen::ErrorCorrectionConfig::EDGE_ONLY;
			break;
		default:
			return Error::InvalidTexture;
		}
		msdfgen::generateMSDF(bitmap, shape, transformation, generatorConfig);

		Raster raster{ index, glyphWidth, glyphHeight,
			std::vector<std::uint8_t>(static_cast<std::size_t>(glyphWidth) * glyphHeight * 4) };
		for (int y{}; y < glyphHeight; ++y)
		{
			for (int x{}; x < glyphWidth; ++x)
			{
				const auto* channels{ bitmap(x, glyphHeight - y - 1) };
				const auto offset{ (static_cast<std::size_t>(y) * glyphWidth + x) * 4 };
				for (std::size_t channel{}; channel < 3; ++channel)
				{
					const auto value{ std::clamp(
						static_cast<int>(std::lround((channels[channel] + encodedThickness) * 255.0)),
						0, 255) };
					raster.rgba[offset + channel] = static_cast<std::uint8_t>(value);
				}
				raster.rgba[offset + 3] = 255;
			}
		}
		rasters.push_back(std::move(raster));
	}

	std::stable_sort(rasters.begin(), rasters.end(), [](const Raster& lhs, const Raster& rhs)
	{
		return lhs.height > rhs.height;
	});
	constexpr std::uint32_t padding{ 2 };
	std::uint32_t cursorX{ padding };
	std::uint32_t cursorY{ padding };
	std::uint32_t rowHeight{};
	for (const auto& raster : rasters)
	{
		const auto glyphWidth{ static_cast<std::uint32_t>(raster.width) };
		const auto glyphHeight{ static_cast<std::uint32_t>(raster.height) };
		if (glyphWidth + padding * 2 > sdf_font_atlas_width)
			return Error::InvalidTexture;
		if (cursorX + glyphWidth + padding > sdf_font_atlas_width)
		{
			cursorX = padding;
			cursorY += rowHeight + padding;
			rowHeight = 0;
		}
		if (cursorY + glyphHeight + padding > std::numeric_limits<std::uint16_t>::max())
			return Error::InvalidTexture;
		auto& glyph{ font.glyphs[raster.index] };
		glyph.x = static_cast<std::uint16_t>(cursorX);
		glyph.y = static_cast<std::uint16_t>(cursorY);
		glyph.width = static_cast<std::uint16_t>(glyphWidth);
		glyph.height = static_cast<std::uint16_t>(glyphHeight);
		cursorX += glyphWidth + padding;
		rowHeight = std::max(rowHeight, glyphHeight);
	}
	font.width = sdf_font_atlas_width;
	font.height = (cursorY + rowHeight + padding + 3) & ~3u;
	font.pixels.assign(static_cast<std::size_t>(font.width) * font.height * 4, 0);
	for (const auto& raster : rasters)
	{
		const auto& glyph{ font.glyphs[raster.index] };
		for (std::uint32_t y{}; y < glyph.height; ++y)
		{
			const auto sourceOffset{ static_cast<std::size_t>(y) * glyph.width * 4 };
			const auto targetOffset{
				(static_cast<std::size_t>(glyph.y + y) * font.width + glyph.x) * 4 };
			std::memcpy(font.pixels.data() + targetOffset, raster.rgba.data() + sourceOffset,
				static_cast<std::size_t>(glyph.width) * 4);
		}
	}
	return Error::Success;
}
