// The one translation unit that compiles miniaudio (and the Vorbis decoder it uses).
#if SWIM_AUDIO_MINIAUDIO
#define STB_VORBIS_HEADER_ONLY
#include <extras/stb_vorbis.c>
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#undef STB_VORBIS_HEADER_ONLY
#include <extras/stb_vorbis.c>
#endif
