#include "src/system/core/config.h"
// Normal service-shaped replies, real JSON/parser/stream selection. No network.
#define http_request fixture_request
#include "src/system/streaming/zvuk.c"
#undef http_request
#include <assert.h>

static const char *reply;
static int calls;
static bool restored_episode;
const char *tr(const char *key) { return key; }
const char *stub_config_path(void) { return NULL; }
const char *config_path_copy(char *out, size_t size) {
 const char *v=stub_config_path(); if(v && size) snprintf(out,size,"%s",v); return v ? out : NULL;
}

const char *http_last_content_type(void) { return "application/json"; }
const char *stub_config_get(const char *section, const char *key, const char *fallback) {
	(void)section;
	(void)key;
	return fallback;
}
const char *config_get_copy(const char *section, const char *key, const char *fallback, char *out, size_t size) {
 const char *v=stub_config_get(section,key,fallback); if(v && size) snprintf(out,size,"%s",v); return v ? out : NULL;
}

void streamerr_hint(stream_err_t kind) { (void)kind; }
const char *http_last_error(void) { return NULL; }
void http_last_outcome(http_outcome_t *out) { memset(out, 0, sizeof(*out)); }
bool fixture_request(const char *url, const http_req_t *req, char **body, size_t *size, size_t limit, int timeout, int *status) {
	(void)url;
	(void)size;
	(void)limit;
	(void)timeout;
	calls++;
	assert(req->body && !strncmp(req->method, "POST", 4));
	if (restored_episode && calls == 1) {
		assert(strstr(req->body, "on Track") && !strstr(req->body, "on Episode"));
		*body = strdup("{\"data\":{\"mediaContents\":[{\"__typename\":\"Episode\"}]}}");
	} else {
		*body = strdup(reply);
	}
	*status = 200;
	return true;
}

int main(void) {
	strcpy(token, "fixture");
	strcpy(base_url, "http://fixture");
	zvuk_spoken_part_t parts[3];
	zvuk_spoken_t book;
	reply = "{\"data\":{\"getBooks\":[{\"title\":\"Normal book\",\"fullDuration\":600,\"bookAuthors\":[{\"rname\":\"Author\"}],\"performers\":[{\"rname\":\"Narrator\"}],\"chapters\":[{\"__typename\":\"Chapter\",\"id\":\"103\",\"title\":\"Third\",\"position\":3,\"duration\":300},{\"__typename\":\"Chapter\",\"id\":\"101\",\"title\":\"First\",\"position\":1,\"duration\":100},{\"__typename\":\"Chapter\",\"id\":\"102\",\"title\":\"Second\",\"position\":2,\"duration\":200}]}]}}";
	assert(zvuk_book_parts("28850372", parts, 3, &book) == 3);
	assert(!strcmp(book.author, "Author") && !strcmp(book.narrator, "Narrator"));
	assert(!strcmp(parts[0].track.id, "101") && parts[2].track.track_number == 3);
	assert(parts[0].track.content == ZVUK_CONTENT_CHAPTER && book.duration == 600);
	assert(zvuk_book_parts("28850372", parts, 2, &book) < 0 && book.part_count == 0);
	// The user's 338-episode show must not be capped at the book's 300 parts.
	char podcast[20000];
	size_t used = (size_t)snprintf(podcast, sizeof(podcast), "{\"data\":{\"getPodcasts\":[{\"title\":\"Normal show\",\"episodes\":[");
	for (int i = 0; i < 338; i++)
		used += (size_t)snprintf(podcast + used, sizeof(podcast) - used, "%s{\"id\":\"%d\",\"publicationDate\":\"%s\"}", i ? "," : "", 1000 + i, i == 0 ? "2025-01-01" : i == 337 ? "2026-10-17" : "");
	snprintf(podcast + used, sizeof(podcast) - used, "]}]}}");
	reply = podcast;
	char (*ids)[ZVUK_ID_MAX] = NULL;
	assert(zvuk_podcast_ids("12874277", &ids, &book) == 338);
	assert(book.part_count == 338 && !strcmp(ids[0], "1337") && !strcmp(ids[1], "1000"));
	free(ids);
	// Normal nullable GraphQL slots: two removed episodes do not hide a show.
	used = (size_t)snprintf(podcast, sizeof(podcast), "{\"data\":{\"getPodcasts\":[{\"title\":\"Normal show\",\"episodes\":[");
	for (int i = 0; i < 338; i++) {
		if (i == 262 || i == 263)
			used += (size_t)snprintf(podcast + used, sizeof(podcast) - used, ",null");
		else
			used += (size_t)snprintf(podcast + used, sizeof(podcast) - used, "%s{\"id\":\"%d\",\"publicationDate\":\"%s\"}",
				i ? "," : "", 1000 + i, i == 0 ? "2025-01-01" : i == 337 ? "2026-10-17" : "");
	}
	snprintf(podcast + used, sizeof(podcast) - used, "]}]}}");
	reply = podcast;
	assert(zvuk_podcast_ids("12874277", &ids, &book) == 336);
	assert(book.part_count == 336 && !strcmp(ids[0], "1337") && !strcmp(ids[1], "1000"));
	free(ids);
	reply = "{\"data\":{\"getPodcasts\":[{\"title\":\"Empty show\",\"episodes\":[]}]}}";
	assert(zvuk_podcast_ids("12874277", &ids, &book) == 0 && ids == NULL && book.part_count == 0);
	reply = "{\"data\":{\"getPodcasts\":[{\"title\":\"Unavailable show\",\"episodes\":[null,null]}]}}";
	assert(zvuk_podcast_ids("12874277", &ids, &book) < 0 && ids == NULL);
	reply = "{\"data\":{\"getPodcasts\":[{\"title\":\"Incomplete show\",\"episodes\":[{\"title\":\"No ID supplied\"}]}]}}";
	assert(zvuk_podcast_ids("12874277", &ids, &book) < 0 && ids == NULL);
	char *large = malloc(60000);
	assert(large);
	used = (size_t)snprintf(large, 60000, "{\"data\":{\"getPodcasts\":[{\"title\":\"Large show\",\"episodes\":[");
	for (int i = 0; i < 10001; i++) used += (size_t)snprintf(large + used, 60000 - used, "%snull", i ? "," : "");
	snprintf(large + used, 60000 - used, "]}]}}");
	reply = large;
	assert(zvuk_podcast_ids("12874277", &ids, &book) < 0 && ids == NULL);
	free(large);
	reply = "{\"data\":{\"getEpisodes\":[{\"__typename\":\"Episode\",\"id\":\"201\",\"title\":\"Normal episode\",\"duration\":180,\"publicationDate\":\"2026-10-17T00:00:00Z\",\"podcast\":{\"id\":\"12874277\",\"title\":\"Normal show\",\"authors\":[{\"name\":\"Host\"}]}}]}}";
	const char *episode_id = "201";
	zvuk_track_t episode;
	assert(zvuk_episodes(&episode_id, 1, &episode, 1) == 1);
	assert(episode.content == ZVUK_CONTENT_EPISODE && !strcmp(episode.artist, "Host") && episode.published_at[0]);
	zvuk_album_t found;
	reply = "{\"data\":{\"search\":{\"books\":{\"items\":[{\"id\":\"28850372\",\"title\":\"Normal book\",\"authorNames\":[\"Author\",\"Coauthor\"]}]}}}}";
	assert(zvuk_search("Normal book", ZVUK_SEARCH_BOOKS, &found, 1) == 1);
	assert(!strcmp(found.id, "28850372") && !strcmp(found.artist, "Author, Coauthor") && !found.cover[0]);
	reply = "{\"data\":{\"search\":{\"podcasts\":{\"items\":[{\"id\":\"12874277\",\"title\":\"Normal show\",\"image\":{\"src\":\"https://fixture.invalid/cover\"},\"authors\":[{\"name\":\"Host\"}]}]}}}}";
	assert(zvuk_search("Normal show", ZVUK_SEARCH_PODCASTS, &found, 1) == 1);
	assert(!strcmp(found.artist, "Host") && found.cover[0]);
	static const char *const fields[] = {"tracks", "releases", "artists", "playlists"};
	for (int kind = 0; kind < 4; kind++) {
		char response[300];
		snprintf(response, sizeof(response), "{\"data\":{\"search\":{\"%s\":{\"items\":[{\"id\":\"123\",\"title\":\"Original music result\"}]}}}}", fields[kind]);
		reply = response;
		union {
			zvuk_track_t track;
			zvuk_album_t album;
			zvuk_artist_t artist;
			zvuk_playlist_t playlist;
		} result;
		assert(zvuk_search("Music", (zvuk_search_kind_t)kind, &result, 1) == 1);
		assert(!strcmp(result.track.id, "123")); // all existing result types begin with id
	}
	reply = "{\"data\":{\"mediaContents\":[{\"__typename\":\"Chapter\",\"stream\":{\"high\":\"https://fixture.invalid/chapter.mp3\",\"mid\":\"https://fixture.invalid/chapter-small.mp3\"}}]}}";
	zvuk_stream_t stream;
	calls = 0;
	assert(zvuk_spoken_stream("101", &stream) && calls == 1 && stream.got == ZVUK_QUALITY_HIGH);
	restored_episode = true;
	calls = 0;
	assert(zvuk_stream("201", &stream) && calls == 2 && stream.got == ZVUK_QUALITY_HIGH);
	restored_episode = false;
	calls = 0;
	reply = "{\"data\":{\"mediaContents\":[{\"__typename\":\"Track\",\"stream\":{\"flac\":\"https://fixture.invalid/music.flac\",\"high\":\"https://fixture.invalid/music.mp3\"}}]}}";
	assert(zvuk_stream("301", &stream) && calls == 1 && stream.got == ZVUK_QUALITY_FLAC);
	puts("PASS: complete sorted book, capacity refusal, 338 compact episode IDs, episode metadata, spoken search + original four kinds, typed/fallback streams, unchanged music FLAC");
	return 0;
}
