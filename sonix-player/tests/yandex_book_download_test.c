/* Real book worker/publication/tagging with normal text transport stand-ins.
 * No credentials, network, device, or audio decoding.
 * cc -std=gnu11 -O1 -DHOST_BUILD -DLV_CONF_INCLUDE_SIMPLE -I.
 * -ffunction-sections -fdata-sections tests/yandex_book_download_test.c
 * src/system/streaming/bookdownload.c src/system/streaming/streamcover.c
 * src/system/library/mp4flac.c build_host/system/image/stb_image_impl.o
 * -Wl,--gc-sections -pthread -lm -o /tmp/book-download && /tmp/book-download
 */
#define _GNU_SOURCE 1
#include "src/system/streaming/yandexdl.c"
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <setjmp.h>

static char card[400];
static int metadata_requests, audio_requests, scan_requests, last_pin, fail_part;
static bool unavailable, deny_pin, cancel_transfer, change_card;
static int cancelled, music_index_writes, music_scans, podcast_tags;
static bool fail_podcast_tags, wrong_episode_namespace;
static jmp_buf worker_done;
const char *tr(const char *key) { return key; }
const char *storage_sd_root(void) { return card; }
bool storage_card_attached(void) { return true; }
bool storage_sd_writable(void) { return true; }
bool usb_storage_active(void) { return false; }
int storage_pin_card(char *out, size_t size) {
	if (deny_pin)
		return -1;
	snprintf(out, size, "%s", card);
	last_pin = open(card, O_RDONLY | O_DIRECTORY);
	return last_pin;
}
void audiobookdb_folder(char *out, size_t size) { snprintf(out, size, "Books/Audio"); }
void audiobookdb_request_scan(const char *path) {
	assert(strstr(path, "/Books/Audio"));
	scan_requests++;
}
bool gui_post(void (*cb)(void *), void *user) {
	cb(user);
	return true;
}
char **library_scan_folders(int *count) {
	*count = 0;
	return NULL;
}
void library_scan_folders_free(char **names, int count) {
	(void)names;
	(void)count;
}
void podcastdl_safe_name(const char *s, const char *fallback, char *out, size_t size) { snprintf(out, size, "%s", s[0] ? s : fallback); }
bool podcastimport_paths(const char *r, const char *service, const char *id, const char *show, const char *ep, const char *title, char *folder, size_t fs, char *base, size_t bs) {
	(void)show;
	(void)title;
	assert(strcmp(service, "yandex") == 0 && strcmp(id, "77") == 0 && strcmp(ep, "600") == 0);
	snprintf(folder, fs, "%s/Podcast/Show", r);
	snprintf(base, bs, "Episode");
	return true;
}
bool podcastimport_write_tags(const char *p, const char *service, const char *id, const char *show, const char *ep, const char *title, const char *artist, const char *cover, const char *date) {
	(void)show;
	(void)title;
	(void)artist;
	(void)cover;
	(void)date;
	assert(strcmp(service, "yandex") == 0 && strcmp(id, "77") == 0 && strcmp(ep, "600") == 0);
	podcast_tags++;
	char final[512];
	snprintf(final, sizeof(final), "%s/Podcast/Show/Episode.mp3", card);
	assert(strstr(p, "/.Episode.mp3.part") && access(final, F_OK) != 0);
	if (fail_podcast_tags)
		return false;
	char sidecar[600];
	snprintf(sidecar, sizeof(sidecar), "%s.tags", p);
	FILE *f = fopen(sidecar, "w");
	assert(f);
	assert(fputs("source_episode_id=600\n", f) >= 0 && fclose(f) == 0);
	return true;
}
bool podcastimport_key(const char *path, char *out, size_t size) {
	char sidecar[600], line[80];
	snprintf(sidecar, sizeof(sidecar), "%s.tags", path);
	FILE *f = fopen(sidecar, "r");
	if (!f)
		return false;
	bool ok = fgets(line, sizeof(line), f) && strcmp(line, "source_episode_id=600\n") == 0;
	fclose(f);
	if (ok)
		snprintf(out, size, "s:%s:600", wrong_episode_namespace ? "zvuk" : "yandex");
	return ok;
}

int yandex_album_tracks_complete(const char *id, yandex_track_t *out, int max, yandex_album_meta_t *m) {
	assert(fcntl(last_pin, F_GETFD) < 0);
	assert(max >= 13);
	metadata_requests++;
	memset(m, 0, sizeof(*m));
	strcpy(m->id, id);
	strcpy(m->title, "Complete Book");
	m->content_type = YANDEX_CONTENT_AUDIOBOOK;
	m->track_count = 13;
	m->discs = 1;
	strcpy(m->cover, "https://example.invalid/optional-artwork");
	for (int i = 0; i < 13; i++) {
		memset(&out[i], 0, sizeof(out[i]));
		snprintf(out[i].id, sizeof(out[i].id), "%d", 144069784 + i);
		strcpy(out[i].album_id, id);
		strcpy(out[i].album, m->title);
		snprintf(out[i].title, sizeof(out[i].title), "Chapter %d", i + 1);
		out[i].available = !(unavailable && i == 4);
		out[i].track_number = i + 1;
	}
	return 13;
}
bool yandex_album_meta(const char *id, yandex_album_meta_t *out) {
	assert(strcmp(id, "55") == 0 || strcmp(id, "77") == 0);
	memset(out, 0, sizeof(*out));
	strcpy(out->id, id);
	strcpy(out->title, "MusicAlbum");
	strcpy(out->artist, "MusicArtist");
	out->content_type = strcmp(id, "77") == 0 ? YANDEX_CONTENT_PODCAST : YANDEX_CONTENT_MUSIC;
	return true;
}
int yandex_album_tracks(const char *id, yandex_track_t *out, int cap) {
	(void)id;
	(void)out;
	(void)cap;
	assert(!"no Music album expansion here");
	return -1;
}
void thread_be_low_priority(const char *name) { (void)name; }
bool library_card_returned(const char *path) {
	assert(strcmp(path, card) == 0);
	music_scans++;
	return true;
}
static void finished(yandexdl_event_t event, int count, const char *text) {
	(void)count;
	if (event == YANDEXDL_FINISHED) {
		assert(text && !text[0]);
		longjmp(worker_done, 1);
	}
}
const char *yandex_last_error(void) { return "fixture error"; }
bool yandex_file_info(const char *id, yandex_file_t *out) {
	if (strcmp(id, "500") != 0 && strcmp(id, "600") != 0)
		assert(fcntl(last_pin, F_GETFD) < 0);
	memset(out, 0, sizeof(*out));
	strcpy(out->codec, "mp3");
	return true;
}
bool yandexcache_find(const char *id, char *out, size_t size) {
	(void)id;
	(void)out;
	(void)size;
	return false;
}
yandex_quality_t yandex_get_quality(void) { return YANDEX_QUALITY_LOSSLESS; }
const char *yandexcache_extension(const char *codec) {
	(void)codec;
	return "mp3";
}
void yandexcache_fetch_cancel(void) { cancelled++; }
bool yandexcache_fetch(const char *id, const yandex_file_t *file, FILE *out, void (*progress)(long, long, void *), void *user, uint64_t *samples, char *error, size_t size) {
	(void)id;
	(void)file;
	(void)samples;
	(void)error;
	(void)size;
	audio_requests++;
	assert(fcntl(last_pin, F_GETFD) >= 0);
	if (cancel_transfer && audio_requests == 3)
		generation++;
	if (change_card && audio_requests == 3) {
		generation++;
		card_epoch++;
		root[0] = '\0';
	}
	if (fail_part == audio_requests)
		return false;
	const char bytes[] = "ordinary transport fixture payload";
	bool ok = fwrite(bytes, 1, sizeof(bytes), out) == sizeof(bytes);
	progress(sizeof(bytes), sizeof(bytes), user);
	return ok;
}
bool http_get(const char *url, char **out, size_t *len, size_t cap, int timeout) {
	(void)url;
	(void)out;
	(void)len;
	(void)cap;
	(void)timeout;
	assert(fcntl(last_pin, F_GETFD) < 0);
	return false; // Optional artwork unavailable.
}
void yandexsaved_add(const yandex_track_t *t, const char *a, int y, int n, const char *p) {
	(void)a;
	(void)y;
	(void)n;
	(void)p;
	assert(strcmp(t->id, "500") == 0);
	music_index_writes++;
}
int main(void) {
	char temp[] = "/tmp/yandex-book-XXXXXX", path[512], why[256] = "";
	assert(mkdtemp(temp));
	strcpy(card, temp);
	strcpy(root, temp);
	generation = 1;
	entry_t entry = {.book = true};
	strcpy(entry.track.album_id, "38681851");
	strcpy(entry.book_folder, "Books/Audio");
	unavailable = true;
	assert(!save_book(&entry, 1, path, sizeof(path), why, sizeof(why)) && audio_requests == 0);
	unavailable = false;
	fail_part = 6;
	assert(!save_book(&entry, 1, path, sizeof(path), why, sizeof(why)) && scan_requests == 0);
	char directory[512];
	assert(book_directory(&entry, card, directory, sizeof(directory)));
	assert(access(directory, F_OK) != 0);
	fail_part = audio_requests = 0;
	cancel_transfer = true;
	assert(!save_book(&entry, 1, path, sizeof(path), why, sizeof(why)) && cancelled > 0);
	assert(access(directory, F_OK) != 0 && fcntl(last_pin, F_GETFD) < 0);
	cancel_transfer = false;
	generation = 1;
	audio_requests = 0;
	change_card = true;
	assert(!save_book(&entry, 1, path, sizeof(path), why, sizeof(why)));
	char stage[512];
	snprintf(stage, sizeof(stage), "%s/Books/Audio/.yandex-38681851.part", card);
	assert(access(stage, F_OK) == 0); // Never clean paths after a different card appeared.
	change_card = false;
	strcpy(root, card);
	generation = 1;
	audio_requests = 0;
	assert(save_book(&entry, 1, path, sizeof(path), why, sizeof(why)) && audio_requests == 13 && scan_requests == 1);
	assert(strcmp(path, directory) == 0 && fcntl(last_pin, F_GETFD) < 0);
	int network_before = metadata_requests;
	assert(save_book(&entry, 1, path, sizeof(path), why, sizeof(why)));
	assert(metadata_requests == network_before && audio_requests == 13); // Offline repeated download, no rewrites.
	assert(yandexdl_audiobook_path("38681851", path, sizeof(path)));
	deny_pin = true;
	assert(!save_book(&entry, 1, path, sizeof(path), why, sizeof(why)));
	deny_pin = false;
	// Real worker batch: Music followed by a ready book must still rescan Music.
	queue = calloc(2, sizeof(*queue));
	assert(queue);
	queue_len = 2;
	strcpy(queue[0].track.id, "500");
	strcpy(queue[0].track.album_id, "55");
	strcpy(queue[0].track.title, "Song");
	strcpy(queue[0].book_folder, entry.book_folder);
	queue[1] = entry;
	status.active = true;
	status.done = 0;
	status.failed = 0;
	listener = finished;
	if (setjmp(worker_done) == 0)
		worker_main(NULL);
	assert(music_index_writes == 1 && music_scans == 1 && queue_len == 0);
	char song[600];
	snprintf(song, sizeof(song), "%s/Music/Yandex Music/MusicArtist/MusicAlbum/Song.mp3", card);
	assert(unlink(song) == 0);
	snprintf(song, sizeof(song), "%s/Music/Yandex Music/MusicArtist/MusicAlbum", card);
	assert(rmdir(song) == 0);
	snprintf(song, sizeof(song), "%s/Music/Yandex Music/MusicArtist", card);
	assert(rmdir(song) == 0);
	snprintf(song, sizeof(song), "%s/Music/Yandex Music", card);
	assert(rmdir(song) == 0);
	snprintf(song, sizeof(song), "%s/Music", card);
	assert(rmdir(song) == 0);
	// Caller ordering: mandatory episode tags precede its visible audio file.
	yandex_track_t episode = {0};
	strcpy(episode.id, "600");
	strcpy(episode.album_id, "77");
	strcpy(episode.title, "Episode");
	episode.content_type = YANDEX_CONTENT_EPISODE;
	char episode_path[512];
	fail_podcast_tags = true;
	assert(!save_track_with_path(&episode, 1, why, sizeof(why), episode_path, sizeof(episode_path)));
	snprintf(episode_path, sizeof(episode_path), "%s/Podcast/Show/Episode.mp3", card);
	assert(access(episode_path, F_OK) != 0 && music_index_writes == 1);
	fail_podcast_tags = false;
	assert(save_track_with_path(&episode, 1, why, sizeof(why), episode_path, sizeof(episode_path)));
	assert(access(episode_path, F_OK) == 0 && podcast_tags == 2 && music_index_writes == 1);
	char episode_meta[600];
	snprintf(episode_meta, sizeof(episode_meta), "%s.tags", episode_path);
	FILE *epf = fopen(episode_meta, "r");
	char line[80];
	assert(epf && fgets(line, sizeof(line), epf));
	assert(strcmp(line, "source_episode_id=600\n") == 0 && fclose(epf) == 0);
	char local_episode[512];
	assert(yandexdl_local_episode(&episode, local_episode, sizeof(local_episode)) && strcmp(local_episode, episode_path) == 0);
	wrong_episode_namespace = true;
	assert(!yandexdl_local_episode(&episode, local_episode, sizeof(local_episode)) && !local_episode[0]);
	wrong_episode_namespace = false;
	assert(!yandexdl_local_episode(&episode, local_episode, 8));
	assert(unlink(episode_path) == 0 && unlink(episode_meta) == 0);
	snprintf(episode_path, sizeof(episode_path), "%s/Podcast/Show", card);
	assert(rmdir(episode_path) == 0);
	snprintf(episode_path, sizeof(episode_path), "%s/Podcast", card);
	assert(rmdir(episode_path) == 0);
	// Remove only the fixture tree, whose contents this test just created.
	DIR *d = opendir(directory);
	assert(d);
	struct dirent *e;
	while ((e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		assert(unlinkat(dirfd(d), e->d_name, 0) == 0);
	}
	closedir(d);
	assert(rmdir(directory) == 0);
	snprintf(path, sizeof(path), "%s/Books/Audio", card);
	assert(rmdir(path) == 0);
	snprintf(path, sizeof(path), "%s/Books", card);
	assert(rmdir(path) == 0);
	assert(rmdir(card) == 0);
	puts("Yandex 13-part atomic book job, failures and offline repeat: PASS");
}
