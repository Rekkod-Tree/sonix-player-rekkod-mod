// cc -O2 -std=gnu11 -ffunction-sections -fdata-sections -I. -I./lvgl
// -DLV_CONF_INCLUDE_SIMPLE=1 tests/streaming_availability_test.c
// -Wl,--gc-sections -o /tmp/streaming_availability_test
#include "src/gui/streaming/streaming.c"
#include <assert.h>
bool tidal_configured(void) { return true; }
bool qobuz_configured(void) { return true; }
bool podcast_configured(void) { return true; }
bool yandex_configured(void) { return true; }
int main(int argc, char **argv) {
	setenv("SONIX_SPOTIFY_ENGINE", "/sonix-test-no-such-spotify-engine", 1);
	assert(!service_usable(SVC_SPOTIFY));
	setenv("SONIX_SPOTIFY_ENGINE", argc > 1 ? argv[1] : "/bin/true", 1);
	assert(service_usable(SVC_SPOTIFY));
	assert(service_usable(SVC_YANDEX) && service_usable(SVC_RADIO) && service_usable(SVC_SUBSONIC));
	puts("PASS missing Spotify transport is unavailable; existing services and executable override remain available");
	return 0;
}
