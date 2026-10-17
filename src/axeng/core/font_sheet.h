#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "axeng/core/error.h"

namespace ax
{
	inline constexpr uint32_t font_sheet_width{ 256 };
	inline constexpr uint32_t font_sheet_height{ 256 };

	Error generate_source_code_pro_font_sheet(std::span<const uint8_t> fontData, std::vector<uint8_t>& pixels);
	bool is_font_texture_name(std::string_view name) noexcept;
}
