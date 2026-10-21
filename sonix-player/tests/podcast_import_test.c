/* Normal local episodes and simulated storage refusal; no network or decoder.
From sonix-player (Linux, bundled SQLite):
cc -std=gnu11 -O1 -ffunction-sections -fdata-sections -I. -I./lvgl \
  tests/podcast_import_test.c src/system/streaming/podcastimport.c \
  src/system/streaming/podcastdl.c src/system/streaming/podcastcache.c \
  src/system/core/utils.c src/system/db/sqlite3_impl.c \
  -Wl,--gc-sections,--wrap=renameat,--wrap=fclose -pthread -ldl -lm \
  -o /tmp/podcast_import_test && /tmp/podcast_import_test
*/
#include "src/system/streaming/podcaststate.c"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <unistd.h>

static char fixture_root[] = "/tmp/sonix-podcast-import-XXXXXX";
static bool admitted = true;
static bool move_on_pin, refuse_rename, refuse_close;
static char detached[600];
int __real_renameat(int, const char *, int, const char *);
int __wrap_renameat(int from, const char *old, int to, const char *next) {
	if (refuse_rename) {
		errno = EIO;
		return -1;
	}
	return __real_renameat(from, old, to, next);
}
int __real_fclose(FILE *);
int __wrap_fclose(FILE *file) {
	int result = __real_fclose(file);
	if (refuse_close) {
		errno = ENOSPC;
		return EOF;
	}
	return result;
}
const char *storage_sd_root(void) { return fixture_root; }
int storage_pin_card(char *out, size_t size) {
	if (!admitted)
		return -1;
	snprintf(out, size, "%s", fixture_root);
	int fd = open(fixture_root, O_RDONLY | O_DIRECTORY);
	if (move_on_pin) {
		snprintf(detached, sizeof(detached), "%s-detached", fixture_root);
		assert(rename(fixture_root, detached) == 0);
		assert(mkdir(fixture_root, 0700) == 0);
	}
	return fd;
}
long config_get_int(const char *section, const char *key, long fallback) {
	(void)section;
	(void)key;
	return fallback;
}
void metadata_read(const char *path, song_metadata_t *out) {
	(void)path;
	memset(out, 0, sizeof(*out));
}

static void create_episode(const char *service, const char *id, const char *title, char *path, size_t size) {
	char folder[512], base[180];
	assert(podcastimport_paths(fixture_root, service, "11002256", "A normal show", id, title, folder, sizeof(folder), base, sizeof(base)));
	assert(mkdir(folder, 0700) == 0 || errno == EEXIST);
	assert(snprintf(path, size, "%s/%s.wav", folder, base) < (int)size);
	// A valid mono 16-bit PCM WAV with one zero sample. It is never decoded.
	const unsigned char wav[] = {'R', 'I', 'F', 'F', 38, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0, 0x44, 0xac, 0, 0, 0x88, 0x58, 1, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a', 2, 0, 0, 0, 0, 0};
	FILE *f = fopen(path, "wb");
	assert(f && fwrite(wav, 1, sizeof(wav), f) == sizeof(wav));
	assert(fclose(f) == 0);
	assert(podcastimport_write_tags(path, service, "11002256", "A normal show", id, title, "Author", "", "2026-10-17T12:30:00+03:00"));
}

int main(void) {
	assert(mkdtemp(fixture_root));
	char folder[600];
	snprintf(folder, sizeof(folder), "%s/Podcast", fixture_root);
	assert(mkdir(folder, 0700) == 0);
	char a[512], b[512], z[512];
	create_episode("yandex", "156882082", "First episode", a, sizeof(a));
	create_episode("yandex", "156882083", "Second episode", b, sizeof(b));
	create_episode("zvuk", "156882082", "First episode", z, sizeof(z));
	assert(strcmp(a, z) != 0);
	// The downloader can finish metadata while the audio is still hidden.
	char stage[512], stage_tags[560], final_tags[560];
	const char *base_name = strrchr(b, '/');
	snprintf(stage, sizeof(stage), "%.*s/.%s.part", (int)(base_name - b), b, base_name + 1);
	snprintf(stage_tags, sizeof(stage_tags), "%s.tags", stage);
	snprintf(final_tags, sizeof(final_tags), "%s.tags", b);
	assert(rename(b, stage) == 0 && unlink(final_tags) == 0);
	assert(podcastimport_write_tags(stage, "yandex", "11002256", "A normal show", "156882083", "Second episode", "", "", ""));
	assert(access(b, F_OK) != 0);
	char stage_key[80];
	assert(!podcastimport_key(stage, stage_key, sizeof(stage_key)));
	assert(!key_for(stage, 0, stage_key, sizeof(stage_key)));
	assert(rename(stage_tags, final_tags) == 0);
	assert(access(b, F_OK) != 0); // Sidecar alone is not a visible episode.
	assert(rename(stage, b) == 0);
	assert(podcastimport_key(b, stage_key, sizeof(stage_key)) && strcmp(stage_key, "s:yandex:156882083") == 0);
	assert(podcastcache_is_episode(a));
	assert(podcastcache_episode_id(a) == 0); // Not a Podcast Index episode ID.
	long long feed = 0;
	assert(!podcastcache_feed_id(a, &feed));
	char key[80], other[80];
	assert(podcastimport_key(a, key, sizeof(key)) && strcmp(key, "s:yandex:156882082") == 0);
	assert(podcastimport_key(z, other, sizeof(other)) && strcmp(key, other) != 0);
	struct stat st;
	assert(stat(a, &st) == 0);
	struct tm expected = {.tm_year = 126, .tm_mon = 9, .tm_mday = 17, .tm_hour = 9, .tm_min = 30};
	assert(st.st_mtime == timegm(&expected));

	admitted = false;
	assert(!podcastimport_write_tags(a, "yandex", "11002256", "Other", "156882082", "Changed", "", "", ""));
	admitted = true;
	char title[100];
	assert(podcastcache_tag(a, "title", title, sizeof(title)) && strcmp(title, "First episode") == 0);
	refuse_close = true;
	assert(!podcastimport_write_tags(a, "yandex", "11002256", "Other", "156882082", "Changed", "", "", ""));
	refuse_close = false;
	refuse_rename = true;
	assert(!podcastimport_write_tags(a, "yandex", "11002256", "Other", "156882082", "Changed", "", "", ""));
	refuse_rename = false;
	assert(podcastcache_tag(a, "title", title, sizeof(title)) && strcmp(title, "First episode") == 0);
	move_on_pin = true;
	assert(podcastimport_write_tags(a, "yandex", "11002256", "A normal show", "156882082", "First episode", "", "", ""));
	move_on_pin = false;
	snprintf(folder, sizeof(folder), "%s/Podcast", fixture_root);
	assert(access(folder, F_OK) != 0); // No writes into the replacement mountpoint.
	assert(rmdir(fixture_root) == 0 && rename(detached, fixture_root) == 0);
	assert(!podcastimport_write_tags("/tmp/outside.wav", "yandex", "11002256", "Show", "1", "Title", "", "", ""));
	char tiny[4], base[100];
	assert(!podcastimport_paths(fixture_root, "yandex", "1", "Show", "2", "Title", tiny, sizeof(tiny), base, sizeof(base)));

	// Use the real writer: flush waits for pending and in-flight checkpoints.
	podcaststate_set_root(fixture_root);
	podcaststate_note(a, 60, 3600, true);
	podcaststate_flush();
	assert(fabs(podcaststate_resume_point(a) - 57) < 0.001);
	podcaststate_t catalogue;
	assert(!podcaststate_get_episode(156882082, &catalogue));
	assert(podcaststate_resume_point(b) == 0);
	assert(podcaststate_resume_point(z) == 0);
	podcaststate_note(z, 120, 3600, true);
	podcaststate_flush();
	assert(fabs(podcaststate_resume_point(z) - 117) < 0.001);
	assert(fabs(podcaststate_resume_point(a) - 57) < 0.001);
	podcaststate_set_root(NULL);
	podcaststate_set_root(fixture_root);
	assert(fabs(podcaststate_resume_point(a) - 57) < 0.001);

	char renamed[512], old_tags[560], new_tags[560];
	snprintf(renamed, sizeof(renamed), "%s", a);
	char *name = strrchr(renamed, '/');
	snprintf(name + 1, sizeof(renamed) - (size_t)(name + 1 - renamed), "Renamed episode.wav");
	snprintf(old_tags, sizeof(old_tags), "%s.tags", a);
	snprintf(new_tags, sizeof(new_tags), "%s.tags", renamed);
	assert(rename(a, renamed) == 0 && rename(old_tags, new_tags) == 0);
	assert(fabs(podcaststate_resume_point(renamed) - 57) < 0.001);
	podcaststate_note(renamed, 3590, 3600, true);
	podcaststate_flush();
	assert(podcaststate_resume_point(renamed) == 0);
	assert(fabs(podcaststate_resume_point(z) - 117) < 0.001);
	// The shelf deletes media and tags before asking the state store to forget.
	assert(unlink(renamed) == 0 && unlink(new_tags) == 0);
	podcaststate_forget(renamed);
	podcaststate_t forgotten;
	assert(!get_by_key("s:yandex:156882082", &forgotten));
	assert(fabs(podcaststate_resume_point(z) - 117) < 0.001);
	podcaststate_set_root(NULL);
	printf("PASS: local Podcast routing, namespaces, date order, offline resume/reopen/rename, completion, storage refusal\n");
	printf("Fixture directory: %s\n", fixture_root);
	return 0;
}
