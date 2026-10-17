// Provider adapter with real book transaction, Podcast tags and MP3 tag sink.
// Uses a normal MP3 fixture, private temporary directory and no service/device.
#include "src/system/streaming/zvukdl.c"
#include <assert.h>
#include <fcntl.h>

static const char *audio_fixture;
static char card_path[400], temp_root[400];
static bool pin_fds[1024];
static int pins, info_calls, transfers, scans, cancel_at, replace_at;
static int book_parts_count = 3;
static bool fail_publish_audio;
static bool available = true;
int __real_close(int fd);
int __real_rename(const char *from, const char *to);
int __wrap_rename(const char *from, const char *to) {
	size_t length = strlen(to);
	if (fail_publish_audio && strstr(from, "/Podcast/") && strstr(from, ".mp3.part") && length >= 4 && !strcmp(to + length - 4, ".mp3")) {
		fail_publish_audio = false;
		errno = EIO;
		return -1;
	}
	return __real_rename(from, to);
}
int __wrap_close(int fd) {
	if (fd >= 0 && fd < 1024 && pin_fds[fd]) {
		pin_fds[fd] = false;
		pins--;
	}
	return __real_close(fd);
}
int storage_pin_card(char *out, size_t size) {
	if (!available)
		return -1;
	snprintf(out, size, "%s", card_path);
	int fd = open(card_path, O_RDONLY | O_DIRECTORY);
	assert(fd >= 0 && fd < 1024);
	pin_fds[fd] = true;
	pins++;
	return fd;
}
bool storage_card_attached(void) { return available; }
bool storage_sd_writable(void) { return available; }
bool usb_storage_active(void) { return false; }
const char *storage_sd_root(void) { return available ? card_path : NULL; }
const char *tr(const char *key) { return key; }
const char *zvuk_last_error(void) { return "fixture error"; }
const char *zvuk_stream_headers(void) { return ""; }
void audiobookdb_folder(char *out, size_t size) { snprintf(out, size, "Audiobooks"); }
void audiobookdb_request_scan(const char *path) {
	assert(strstr(path, "/Audiobooks") && pins == 0);
	scans++;
}
bool gui_post(void (*fn)(void *), void *user) {
	fn(user);
	return true;
}
void podcastdl_safe_name(const char *name, const char *fallback, char *out, size_t size) { snprintf(out, size, "%s", name && name[0] ? name : fallback); }
bool podcastdl_owns(const char *path) { return strstr(path, "/Podcast/") != NULL; }
bool podcastcache_tag(const char *path, const char *key, char *out, size_t size) {
	char tags[600];
	snprintf(tags, sizeof(tags), "%s.tags", path);
	FILE *f = fopen(tags, "rb");
	if (!f)
		return false;
	char line[1024];
	bool found = false;
	while (fgets(line, sizeof(line), f)) {
		char *equal = strchr(line, '=');
		if (!equal)
			continue;
		*equal++ = '\0';
		if (!strcmp(line, key)) {
			equal[strcspn(equal, "\r\n")] = '\0';
			snprintf(out, size, "%s", equal);
			found = true;
			break;
		}
	}
	fclose(f);
	return found;
}
bool http_get_ex(const char *url, const char *headers, bool errors, char **body, size_t *size, size_t cap, int timeout, int *status) {
	(void)url;
	(void)headers;
	(void)errors;
	(void)cap;
	(void)timeout;
	(void)status;
	assert(pins == 0);
	*body = NULL;
	*size = 0;
	return false;
}
bool streamcover_store(const char *path, const void *data, size_t size) {
	(void)path;
	(void)data;
	(void)size;
	return false;
}
int zvuk_book_parts(const char *id, zvuk_spoken_part_t *parts, int max, zvuk_spoken_t *meta) {
	assert(pins == 0 && max >= 3);
	info_calls++;
	memset(meta, 0, sizeof(*meta));
	strcpy(meta->title, "Normal book");
	meta->part_count = book_parts_count;
	strcpy(meta->cover, "https://fixture.invalid/cover");
	for (int i = 0; i < book_parts_count; i++) {
		memset(&parts[i], 0, sizeof(parts[i]));
		zvuk_track_t *t = &parts[i].track;
		snprintf(t->id, sizeof(t->id), "%d", 101 + i);
		snprintf(t->title, sizeof(t->title), "Chapter %d", i + 1);
		snprintf(t->album_id, sizeof(t->album_id), "%s", id);
		strcpy(t->album, "Normal book");
		strcpy(t->artist, "Author");
		t->content = ZVUK_CONTENT_CHAPTER;
		t->track_number = i + 1;
		t->available = true;
	}
	return book_parts_count;
}
bool zvuk_spoken_stream(const char *id, zvuk_stream_t *stream) {
	assert(pins == 0 && id[0]);
	info_calls++;
	memset(stream, 0, sizeof(*stream));
	stream->got = ZVUK_QUALITY_HIGH;
	return true;
}
void zvukcache_fetch_cancel(void) {}
bool zvukcache_fetch(const char *id, const zvuk_stream_t *stream, FILE *out, void (*progress)(long, long, void *), void *user, uint64_t *samples, char *err, size_t error_size) {
	(void)id;
	(void)stream;
	(void)samples;
	(void)err;
	(void)error_size;
	assert(pins == 1);
	transfers++;
	FILE *in = fopen(audio_fixture, "rb");
	assert(in);
	char bytes[1024];
	size_t n;
	while ((n = fread(bytes, 1, sizeof(bytes), in)))
		assert(fwrite(bytes, 1, n, out) == n);
	fclose(in);
	if (transfers == cancel_at)
		generation++;
	if (transfers == replace_at) {
		char old[450], dir[512], sentinel[540];
		snprintf(old, sizeof(old), "%s/card-old", temp_root);
		assert(rename(card_path, old) == 0 && mkdir(card_path, 0755) == 0);
		snprintf(dir, sizeof(dir), "%s/Audiobooks/.zvuk-777.part", card_path);
		assert(make_dirs(card_path, dir));
		snprintf(sentinel, sizeof(sentinel), "%s/untouched", dir);
		FILE *f = fopen(sentinel, "wb");
		assert(f);
		fputs("new card", f);
		fclose(f);
		card_epoch++;
		generation++;
	}
	progress(100, 100, user);
	return transfers != cancel_at && transfers != replace_at;
}
int main(int argc, char **argv) {
	assert(argc == 3);
	audio_fixture = argv[1];
	snprintf(temp_root, sizeof(temp_root), "%s", argv[2]);
	assert(strlen(temp_root) + 6 < sizeof(card_path));
	strcpy(card_path, temp_root);
	strcat(card_path, "/card");
	assert(mkdir(card_path, 0755) == 0);
	strcpy(root, card_path);
	generation = card_epoch = 1;
	entry_t entry = {.book = true};
	strcpy(entry.track.album_id, "555");
	strcpy(entry.book_folder, "Audiobooks");
	char path[512], why[256];
	assert(save_book(&entry, generation, path, sizeof(path), why, sizeof(why)));
	assert(bookdownload_ready(path, NULL, 0) && transfers == 3 && scans == 1 && pins == 0 && saved_count == 0);
	int calls = info_calls;
	assert(save_book(&entry, generation, path, sizeof(path), why, sizeof(why)) && info_calls == calls);
	book_parts_count = 1;
	strcpy(entry.track.album_id, "556");
	assert(save_book(&entry, generation, path, sizeof(path), why, sizeof(why)));
	assert(strstr(path, "/0001-101.mp3"));
	char value[256];
	assert(podcastcache_tag(path, "title", value, sizeof(value)) && !strcmp(value, "Normal book"));
	assert(podcastcache_tag(path, "album_artist", value, sizeof(value)) && !strcmp(value, "Author"));
	book_parts_count = 3;
	// A cancelled new book never publishes, and leaves the completed one intact.
	strcpy(entry.track.album_id, "666");
	cancel_at = transfers + 2;
	assert(!save_book(&entry, generation, path, sizeof(path), why, sizeof(why)) && pins == 0);
	char failed[512];
	snprintf(failed, sizeof(failed), "%s/Audiobooks/zvuk-666", card_path);
	assert(access(failed, F_OK) != 0);
	cancel_at = 0;
	// One local episode publishes audio+namespaced tags, with no Music index.
	zvuk_track_t episode = {.content = ZVUK_CONTENT_EPISODE};
	strcpy(episode.id, "201");
	strcpy(episode.album_id, "901");
	strcpy(episode.album, "Normal show");
	strcpy(episode.title, "Normal episode");
	strcpy(episode.artist, "Host");
	strcpy(episode.published_at, "2026-10-17T00:00:00Z");
	assert(save_episode(&episode, generation, path, sizeof(path), why, sizeof(why)) && pins == 0 && saved_count == 0);
	char tags[540];
	snprintf(tags, sizeof(tags), "%s.tags", path);
	FILE *f = fopen(tags, "rb");
	assert(f);
	char text[2048] = "";
	assert(fread(text, 1, sizeof(text) - 1, f));
	fclose(f);
	assert(strstr(text, "source_service=zvuk") && strstr(text, "source_episode_id=201") && !strstr(text, "\nfeed_id="));
	calls = info_calls;
	assert(save_episode(&episode, generation, path, sizeof(path), why, sizeof(why)) && calls == info_calls);
	f = fopen(tags, "wb");
	assert(f);
	fputs("source_service=yandex\nsource_episode_id=201\n", f);
	fclose(f);
	assert(save_episode(&episode, generation, path, sizeof(path), why, sizeof(why)) && calls == info_calls);
	assert(podcastimport_key(path, value, sizeof(value)) && !strcmp(value, "s:zvuk:201"));
	strcpy(episode.id, "202");
	fail_publish_audio = true;
	assert(!save_episode(&episode, generation, path, sizeof(path), why, sizeof(why)) && pins == 0);
	snprintf(tags, sizeof(tags), "%s.tags", path);
	assert(!exists(path) && !exists(tags)); // failed final rename leaves no orphan metadata
	// Physical replacement: keep private old stage, never clean a new card's path.
	strcpy(entry.track.album_id, "777");
	replace_at = transfers + 2;
	assert(!save_book(&entry, generation, path, sizeof(path), why, sizeof(why)) && pins == 0);
	snprintf(failed, sizeof(failed), "%s/Audiobooks/.zvuk-777.part/untouched", card_path);
	assert(exists(failed));
	puts("PASS: complete/single book, offline repeat, cancellation, per-transfer pins, episode namespace repair, publish failure, physical replacement cleanup guard");
	return 0;
}
