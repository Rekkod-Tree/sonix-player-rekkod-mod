// Link against the same target archives as the player, then run on the device.
#include <ft2build.h>
#include FT_FREETYPE_H
#include <opus/opus.h>
#include <wavpack/wavpack.h>
#include <assert.h>
#include <stdio.h>

int main(void) {
	FT_Library library;
	assert(FT_Init_FreeType(&library) == 0);
	FT_Int major, minor, patch;
	FT_Library_Version(library, &major, &minor, &patch);
	assert(major == FREETYPE_MAJOR && minor == FREETYPE_MINOR && patch == FREETYPE_PATCH);
	printf("FreeType %d.%d.%d\nOpus %s\nWavPack %s\n", major, minor, patch,
		   opus_get_version_string(), WavpackGetLibraryVersionString());
	FT_Done_FreeType(library);
	return 0;
}
