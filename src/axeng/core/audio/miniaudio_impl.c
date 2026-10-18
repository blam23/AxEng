// Single translation unit for the miniaudio implementation, with stb_vorbis for Ogg Vorbis decoding.
// stb_vorbis's implementation must come after miniaudio's.
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#include "miniaudio.h"

#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
