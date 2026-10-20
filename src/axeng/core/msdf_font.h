#pragma once

#include "axeng/core/sdf_font.h"

namespace ax
{
	inline constexpr float msdf_font_pixel_height{ 96.0f };
	inline constexpr int msdf_font_spread{ 16 };

	enum class MsdfEdgeColoring
	{
		Simple,
		ByDistance
	};

	enum class MsdfErrorCorrection
	{
		Disabled,
		EdgePriority,
		EdgeOnly
	};

	struct MsdfFontSettings
	{
		float pixelHeight{ msdf_font_pixel_height };
		int spread{ msdf_font_spread };
		float thickness{};
		double edgeAngleThreshold{ 3.0 };
		MsdfEdgeColoring edgeColoring{ MsdfEdgeColoring::ByDistance };
		MsdfErrorCorrection errorCorrection{ MsdfErrorCorrection::EdgePriority };
	};

	Error generate_msdf_font(std::span<const uint8_t> fontData, SdfFont& font,
		const MsdfFontSettings& settings = {});
}
