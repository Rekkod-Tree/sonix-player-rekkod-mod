#include "src/system/core/config.h"
// cc -O2 -std=gnu11 -ffunction-sections -fdata-sections -I. -I./lvgl
// -DLV_CONF_INCLUDE_SIMPLE=1 tests/spotify_transport_test.c -pthread
// -Wl,--gc-sections -o /tmp/spotify_transport_test
// Run with the absolute path of the disposable protocol fixture executable.
#define _GNU_SOURCE
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "src/system/streaming/spotify.c"

static char config_file[512];
const char *stub_config_path(void) { return config_file; }
const char *config_path_copy(char *out, size_t size) {
 const char *v=stub_config_path(); if(v && size) snprintf(out,size,"%s",v); return v ? out : NULL;
}

static char *old_response;
static unsigned old_ticket;
static void *old_request(void *unused) {
	(void)unused;
	old_response = spotify_browse("liked", NULL, NULL, 0, NULL, old_ticket);
	return NULL;
}
int main(int argc, char **argv) {
	assert((argc == 2 || argc == 3) && argv[1][0] == '/');
	bool real = argc == 3 && !strcmp(argv[2], "noauth");
	char temporary[] = "/tmp/sonix-spotify-transport.XXXXXX";
	assert(mkdtemp(temporary));
	snprintf(config_file, sizeof(config_file), "%s/config.ini", temporary);
	setenv("SONIX_SPOTIFY_ENGINE", argv[1], 1);
	setenv("SONIX_SPOTIFY_CATALOG_DELAY", "1", 1);
	paths_init();
	old_ticket = spotify_browse_begin();
	pthread_t thread; assert(!pthread_create(&thread, NULL, old_request, NULL));
	usleep(250000);
	if (real) {
		pthread_join(thread, NULL);
		assert(old_response && strstr(old_response,"auth_required")); free(old_response);
		pid_t same_child = child; assert(same_child > 0 && !kill(same_child,0));
		char *probe = spotify_request("probe",NULL,NULL,0);
		assert(probe && strstr(probe,"\"ok\":true") && child == same_child); free(probe);
		spotify_stop(); assert(child == 0);
		unlink(socket_path); assert(!rmdir(directory)); assert(!rmdir(temporary));
		puts("PASS native no-auth engine survives request-thread exit; same child handles next request; clean stop");
		return 0;
	}
	struct timespec start, end; clock_gettime(CLOCK_MONOTONIC, &start);
	unsigned ticket = spotify_browse_begin();
	char *response = spotify_browse("playlists", NULL, NULL, 0, NULL, ticket);
	clock_gettime(CLOCK_MONOTONIC, &end);
	assert(response && strstr(response, "\"items\"") && strstr(response, "playlist"));
	long elapsed = (end.tv_sec-start.tv_sec)*1000+(end.tv_nsec-start.tv_nsec)/1000000;
	assert(elapsed < 2000);
	free(response);
	pthread_join(thread, NULL); assert(!old_response);
	spotify_stop();
	assert(child == 0);
	unlink(socket_path); assert(!rmdir(directory)); assert(!rmdir(temporary));
	printf("PASS cancelled catalogue request; replacement ready in %ld ms\n", elapsed);
}
