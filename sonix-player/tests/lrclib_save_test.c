// cc -std=gnu11 -O1 -Wall -I. -ffunction-sections -fdata-sections tests/lrclib_save_test.c -Wl,--gc-sections -o /tmp/lrclib_save_test
#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char card[] = "/tmp/sonix-lyrics-XXXXXX";
static char detached[128];
static bool admitted = true, fail_sync, detach_on_pin;
static int fixture_fsync(int fd) { return fail_sync ? -1 : fsync(fd); }
#define fsync fixture_fsync
#include "src/system/library/lrclib.c"
#undef fsync

bool storage_sd_writable(void) { return admitted; }
int storage_pin_card(char *out, size_t size) {
	if (!admitted)
		return -1;
	snprintf(out, size, "%s", card);
	int fd = open(card, O_RDONLY | O_DIRECTORY);
	if (detach_on_pin) {
		snprintf(detached, sizeof(detached), "%s-detached", card);
		assert(rename(card, detached) == 0);
		assert(mkdir(card, 0700) == 0);
	}
	return fd;
}

static void put(const char *path, const char *text) {
	FILE *f = fopen(path, "wb");
	assert(f);
	assert(fputs(text, f) >= 0);
	assert(fclose(f) == 0);
}
static void matches(const char *path, const char *text) {
	char buffer[128] = "";
	FILE *f = fopen(path, "rb");
	assert(f);
	assert(fread(buffer, 1, sizeof(buffer) - 1, f) == strlen(text));
	assert(fclose(f) == 0);
	assert(strcmp(buffer, text) == 0);
}

int main(void) {
	assert(mkdtemp(card));
	char track[256], words[256], directory[256];
	snprintf(directory, sizeof(directory), "%s/album", card);
	assert(mkdir(directory, 0700) == 0);
	snprintf(track, sizeof(track), "%s/album/track.mp3", card);
	snprintf(words, sizeof(words), "%s/album/track.lrc", card);
	put(track, "audio");
	assert(lrclib_save(track, "[00:01]A line"));
	matches(words, "[00:01]A line\n");
	admitted = false;
	assert(!lrclib_save(track, "refused"));
	admitted = true;
	fail_sync = true;
	assert(!lrclib_save(track, "incomplete"));
	fail_sync = false;
	matches(words, "[00:01]A line\n");
	detach_on_pin = true;
	assert(lrclib_save(track, "pinned"));
	assert(access(words, F_OK) != 0);
	snprintf(words, sizeof(words), "%s/album/track.lrc", detached);
	matches(words, "pinned\n");
	unlink(words);
	snprintf(track, sizeof(track), "%s/album/track.mp3", detached);
	unlink(track);
	snprintf(directory, sizeof(directory), "%s/album", detached);
	rmdir(directory);
	rmdir(detached);
	rmdir(card);
	puts("PASS lyrics atomic save, USB refusal, sync failure and detached-mount ownership");
}
