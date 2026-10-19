#include "axeng/core/font_data.h"

namespace
{
	unsigned char mainFont[] =
	{
		#embed "../../../fonts/Reglo-Bold.ttf"
	};
}

std::span<unsigned char> ax::main_font_data() noexcept
{
	return mainFont;
}
