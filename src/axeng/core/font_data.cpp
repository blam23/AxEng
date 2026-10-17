#include "axeng/core/font_data.h"

namespace
{
	unsigned char sourceCodeProFont[] =
	{
		#embed "../../../fonts/SourceCodePro-Medium.ttf"
	};
}

std::span<unsigned char> ax::source_code_pro_font_data() noexcept
{
	return sourceCodeProFont;
}
