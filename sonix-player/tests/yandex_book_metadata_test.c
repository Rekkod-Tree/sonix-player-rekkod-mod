/* cc -std=gnu11 -O1 -Wall -Wextra -ffunction-sections -fdata-sections -I.
 * tests/yandex_book_metadata_test.c src/system/core/json.c
 * src/system/streaming/yandexlist.c -Wl,--gc-sections -pthread -o /tmp/book-meta
 */
#include "src/system/streaming/yandex.c"
#include "src/system/streaming/yandexlist.h"
#include <assert.h>
const char *tr(const char *key) { return key; }

static int parse(const char *text, int cap, yandex_track_t *tracks, yandex_album_meta_t *meta) {
	json_doc_t doc;
	assert(json_parse(text, &doc));
	int count = album_complete_from(&doc, json_root(&doc), "38681851", tracks, cap, meta);
	json_free(&doc);
	return count;
}

static void links(void) {
	char id[YANDEX_ID_MAX];
	const char *valid[] = {
		"https://music.yandex.ru/album/38681851/track/144069784",
		"http://music.yandex.ru/album/38681851?activeTab=track-list",
		"https://music.yandex.com/album/38681851#about",
		"http://music.yandex.com/album/38681851",
	};
	for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
		assert(yandex_album_link_id(valid[i], id, sizeof(id)));
		assert(strcmp(id, "38681851") == 0);
	}
	const char *other[] = {
		"https://music.yandex.ru.example.com/album/38681851", "https://music.yandex.ru@other.example/album/38681851", "https://other.example/album/38681851", "https://books.yandex.ru/reader/F140gdcX?resource=book", "https://music.yandex.ru/artist/38681851", "https://music.yandex.ru/album/", "https://music.yandex.ru/album/38681851wrong", "https://music.yandex.ru/album/-38681851", "38681851",
	};
	for (size_t i = 0; i < sizeof(other) / sizeof(other[0]); i++) {
		strcpy(id, "previous");
		assert(!yandex_album_link_id(other[i], id, sizeof(id)) && !id[0]);
	}
	assert(!yandex_album_link_id(valid[0], id, 8) && !id[0]); // Eight digits need a terminator too.
	assert(yandex_album_link_id(valid[0], id, 9) && strcmp(id, "38681851") == 0);
	assert(!yandex_album_link_id(NULL, id, sizeof(id)) && !id[0]);
	assert(!yandex_album_link_id(valid[0], NULL, 0));
}

static void spoken_search(void) {
	// Both real response sections can contain the same album. metaType cannot
	// distinguish an audiobook from a podcast; only the actual type can.
	const char *response = "{\"result\":{\"podcasts\":{\"results\":["
						   "{\"id\":\"1\",\"title\":\"Book one\",\"type\":\"audiobook\",\"metaType\":\"podcast\"},"
						   "{\"id\":\"2\",\"title\":\"Show\",\"type\":\"podcast\",\"metaType\":\"podcast\"},"
						   "{\"id\":\"9\",\"title\":\"No actual type\",\"metaType\":\"podcast\"}]},"
						   "\"albums\":{\"results\":["
						   "{\"id\":\"1\",\"title\":\"Book one duplicate\",\"type\":\"audiobook\"},"
						   "{\"id\":\"2\",\"title\":\"Show duplicate\",\"type\":\"podcast\"},"
						   "{\"id\":\"3\",\"title\":\"Book two\",\"type\":\"audiobook\"},"
						   "{\"id\":\"4\",\"title\":\"Music\",\"type\":\"album\",\"metaType\":\"podcast\"}]},"
						   "\"podcast_episodes\":{\"results\":["
						   "{\"id\":\"10\",\"title\":\"Episode one\",\"type\":\"podcast-episode\",\"pubDate\":\"2026-10-08\"},"
						   "{\"id\":\"11\",\"title\":\"Music\",\"type\":\"music\"},"
						   "{\"id\":\"12\",\"title\":\"Book chapter\",\"type\":\"audiobook\"},"
						   "{\"id\":\"13\",\"title\":\"Episode two\",\"type\":\"podcast-episode\"}]}}}";
	json_doc_t doc;
	assert(json_parse(response, &doc));
	yandex_album_t shows[3];
	yandex_track_t episodes[3];
	int ns = 3, ne = 3;
	spoken_search_from(&doc, YANDEX_CONTENT_AUDIOBOOK, shows, &ns, episodes, &ne);
	assert(ns == 2 && ne == 0 && strcmp(shows[0].id, "1") == 0 && strcmp(shows[1].id, "3") == 0);
	assert(strcmp(shows[0].title, "Book one") == 0); // First appearance wins across sections.
	ns = ne = 3;
	spoken_search_from(&doc, YANDEX_CONTENT_PODCAST, shows, &ns, episodes, &ne);
	assert(ns == 1 && strcmp(shows[0].id, "2") == 0 && ne == 2);
	assert(strcmp(episodes[0].id, "10") == 0 && strcmp(episodes[1].id, "13") == 0);
	assert(strcmp(episodes[0].pub_date, "2026-10-08") == 0);
	strcpy(shows[1].id, "untouched");
	strcpy(episodes[1].id, "untouched");
	ns = ne = 1;
	spoken_search_from(&doc, YANDEX_CONTENT_PODCAST, shows, &ns, episodes, &ne);
	assert(ns == 1 && ne == 1 && strcmp(shows[1].id, "untouched") == 0 && strcmp(episodes[1].id, "untouched") == 0);
	ns = 1;
	ne = 0;
	spoken_search_from(&doc, YANDEX_CONTENT_AUDIOBOOK, shows, &ns, NULL, &ne);
	assert(ns == 1 && ne == 0 && strcmp(shows[0].id, "1") == 0 && strcmp(shows[1].id, "untouched") == 0);
	ns = ne = 0;
	spoken_search_from(&doc, YANDEX_CONTENT_PODCAST, NULL, &ns, NULL, &ne);
	assert(ns == 0 && ne == 0);
	json_free(&doc);
	assert(json_parse("{\"result\":{}}", &doc));
	ns = ne = 3;
	spoken_search_from(&doc, YANDEX_CONTENT_PODCAST, shows, &ns, episodes, &ne);
	assert(ns == 0 && ne == 0);
	json_free(&doc);
}

static void album_view_compatibility(void) {
	// A real-sized Music album larger than the UI buffer, without pager or
	// trackCount. The old reader returned the bounded prefix, not an error.
	char *text = calloc(40000, 1);
	yandex_track_t *tracks = calloc(301, sizeof(*tracks));
	assert(text && tracks);
	size_t used = (size_t)snprintf(text, 40000, "{\"title\":\"Large Music\",\"type\":\"album\",\"volumes\":[[");
	for (int i = 1; i <= 301; i++) {
		used += (size_t)snprintf(text + used, 40000 - used, "%s{\"id\":\"%d\",\"title\":\"Song %d\",\"available\":%s}", i == 1 ? "" : ",", i, i, i == 1 ? "false" : "true");
	}
	assert(used + 4 < 40000);
	strcpy(text + used, "]]}");
	json_doc_t doc;
	yandex_album_meta_t meta;
	assert(json_parse(text, &doc));
	strcpy(tracks[300].id, "untouched");
	assert(album_view_from(&doc, json_root(&doc), "55", tracks, 300, &meta) == 300);
	assert(meta.content_type == YANDEX_CONTENT_MUSIC && strcmp(meta.title, "Large Music") == 0);
	assert(strcmp(tracks[0].id, "1") == 0 && !tracks[0].available && strcmp(tracks[299].id, "300") == 0);
	assert(strcmp(tracks[300].id, "untouched") == 0);
	json_free(&doc);
	const char *legacy = "{\"title\":\"Legacy Music\",\"metaType\":\"music\",\"volumes\":[["
						 "{\"title\":\"No ID\"},{\"id\":\"2\",\"title\":\"Song\"}]]}";
	assert(json_parse(legacy, &doc));
	assert(album_view_from(&doc, json_root(&doc), "55", tracks, 1, &meta) == 1 && strcmp(tracks[0].id, "2") == 0);
	assert(meta.content_type == YANDEX_CONTENT_UNKNOWN);
	json_free(&doc);
	const char *partial = "{\"title\":\"Incomplete book\",\"type\":\"audiobook\",\"metaType\":\"podcast\","
						  "\"pager\":{\"total\":13},\"volumes\":[[{\"id\":\"1\",\"title\":\"Part one\"}]]}";
	assert(json_parse(partial, &doc));
	assert(album_view_from(&doc, json_root(&doc), "38681851", tracks, 300, &meta) == -1);
	assert(meta.content_type == YANDEX_CONTENT_AUDIOBOOK && strcmp(meta.title, "Incomplete book") == 0);
	assert(strcmp(last_error, "book_download_incomplete") == 0);
	json_free(&doc);
	const char *show = "{\"title\":\"Show\",\"type\":\"podcast\",\"pager\":{\"total\":2},\"volumes\":[["
					   "{\"id\":\"1\",\"title\":\"Episode one\"},{\"id\":\"2\",\"title\":\"Episode two\"}]]}";
	assert(json_parse(show, &doc));
	assert(album_view_from(&doc, json_root(&doc), "11002256", tracks, 1, &meta) == -1);
	assert(meta.content_type == YANDEX_CONTENT_PODCAST && strcmp(last_error, "book_download_limit") == 0);
	json_free(&doc);
	free(tracks);
	free(text);
}

int main(void) {
	links();
	spoken_search();
	album_view_compatibility();
	yandex_track_t tracks[3];
	yandex_album_meta_t meta;
	const char *book = "{\"title\":\"Book\",\"type\":\"audiobook\",\"metaType\":\"podcast\",\"trackCount\":0,"
					   "\"pager\":{\"page\":0,\"total\":2},\"volumes\":[[{\"id\":\"10\",\"title\":\"Part 1\",\"type\":\"audiobook\","
					   "\"available\":true,\"rememberPosition\":true},{\"id\":\"11\",\"title\":\"Part 2\",\"type\":\"audiobook\",\"available\":false}]]}";
	assert(parse(book, 3, tracks, &meta) == 2);
	assert(meta.content_type == YANDEX_CONTENT_AUDIOBOOK && meta.track_count == 2);
	assert(tracks[0].content_type == YANDEX_CONTENT_AUDIOBOOK && tracks[0].remember_position);
	assert(!tracks[1].available && tracks[1].track_number == 2); // Never silently drop a part.
	assert(parse(book, 1, tracks, &meta) == -1);
	assert(strcmp(last_error, "book_download_limit") == 0);
	assert(parse("{\"pager\":{\"total\":3},\"volumes\":[[{\"id\":\"1\",\"title\":\"A\"}]]}", 3, tracks, &meta) == -1);
	assert(parse("{\"pager\":{\"total\":2},\"volumes\":[[{\"id\":\"1\",\"title\":\"A\"},{\"id\":\"1\",\"title\":\"B\"}]]}", 3, tracks, &meta) == -1);
	const char *show = "{\"title\":\"Show\",\"type\":\"podcast\",\"metaType\":\"podcast\",\"pager\":{\"total\":1},"
					   "\"volumes\":[[{\"id\":\"156882082\",\"title\":\"Episode\",\"type\":\"podcast-episode\",\"pubDate\":\"2026-10-17T12:00:00+03:00\","
					   "\"podcastEpisodeType\":\"full\",\"rememberPosition\":true}]]}";
	assert(parse(show, 3, tracks, &meta) == 1 && meta.content_type == YANDEX_CONTENT_PODCAST);
	assert(tracks[0].content_type == YANDEX_CONTENT_EPISODE);
	assert(strcmp(tracks[0].pub_date, "2026-10-17T12:00:00+03:00") == 0);
	tracks[0].track_number = 299;
	yandexlist_t *list = yandexlist_new();
	assert(list && yandexlist_add_track(list, &tracks[0]));
	yandexlist_t *clone = yandexlist_clone(list);
	yandex_track_t copied;
	assert(clone && yandexlist_track(clone, 0, &copied));
	assert(copied.content_type == tracks[0].content_type && copied.remember_position);
	assert(copied.track_number == 299 && strcmp(copied.pub_date, tracks[0].pub_date) == 0);
	assert(strcmp(copied.podcast_episode_type, "full") == 0);
	yandexlist_free(clone);
	yandexlist_free(list);
	puts("Yandex complete book metadata and lazy details: PASS");
}
