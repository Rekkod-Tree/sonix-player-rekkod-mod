/* Local shelf naming only, with real directory counting and sidecar reader.
cc -std=gnu11 -O1 -ffunction-sections -fdata-sections -I. -I./lvgl \
  tests/podcast_import_names_test.c src/system/streaming/podcastcache.c \
  src/system/streaming/podcastdl.c -Wl,--gc-sections -pthread -lm \
  -o /tmp/podcast_import_names_test && /tmp/podcast_import_names_test
*/
#include "src/gui/streaming/podcastcard.c"
#include <assert.h>

static char fixture_root[] = "/tmp/sonix-podcast-names-XXXXXX";
const char *storage_sd_root(void) { return fixture_root; }
bool playlist_is_playable_file(const char *name) {
	const char *dot = strrchr(name, '.');
	return dot && strcmp(dot, ".wav") == 0;
}

static podcastcard_show_t check_show(const char *folder_name, const char *service, const char *expected) {
	podcastcard_show_t show = {0};
	snprintf(show.name, sizeof(show.name), "%s", folder_name);
	snprintf(show.folder, sizeof(show.folder), "%s/Podcast/%s", fixture_root, folder_name);
	assert(mkdir(show.folder, 0700) == 0);
	char path[700];
	snprintf(path, sizeof(path), "%s/Episode.wav", show.folder);
	FILE *file = fopen(path, "wb");
	assert(file);
	// Counting only; no audio decoder is called.
	const unsigned char wav[] = {'R', 'I', 'F', 'F', 38, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0, 0x44, 0xac, 0, 0, 0x88, 0x58, 1, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a', 2, 0, 0, 0, 0, 0};
	assert(fwrite(wav, 1, sizeof(wav), file) == sizeof(wav) && fclose(file) == 0);
	snprintf(path, sizeof(path), "%s/Episode.wav.tags", show.folder);
	file = fopen(path, "w");
	assert(file);
	fprintf(file, "title=Episode\nalbum=The same show\n");
	if (service)
		fprintf(file, "source_service=%s\n", service);
	assert(fclose(file) == 0);
	count_folder(show.folder, &show);
	assert(strcmp(show.name, expected) == 0);
	assert(show.episodes == 1 && show.unheard == 1 && show.fresh && show.downloaded && show.feed_id == 0);
	return show;
}

int main(void) {
	assert(mkdtemp(fixture_root));
	char root[600];
	snprintf(root, sizeof(root), "%s/Podcast", fixture_root);
	assert(mkdir(root, 0700) == 0);
	podcastcard_show_t a = check_show("The same show [yandex-123]", "yandex", "The same show");
	podcastcard_show_t b = check_show("The same show [zvuk-123]", "zvuk", "The same show");
	assert(strcmp(a.folder, b.folder) != 0 && strcmp(a.name, b.name) == 0);
	check_show("My local folder", NULL, "My local folder");
	check_show("Another local folder", "other", "Another local folder");
	puts("PASS: clean imported show names, separate provider folders, unchanged local folder names and counters");
	return 0;
}
