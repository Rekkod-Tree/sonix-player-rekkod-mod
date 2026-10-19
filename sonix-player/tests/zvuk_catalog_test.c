/* Same flags as zvuk_spoken_test.c; link json.c, pthread and --gc-sections. */
#define http_request fixture_request
#include "src/system/streaming/zvuk.c"
#undef http_request
#include <assert.h>

static const char *reply;
static int calls;
static int status_code = 200;
static bool children;
const char *tr(const char *key) { return key; }
const char *config_path(void) { return NULL; }
const char *config_get(const char *section, const char *key, const char *fallback) {
	(void)section;
	(void)key;
	return fallback;
}
const char *http_last_content_type(void) { return "application/json"; }
void streamerr_hint(stream_err_t kind) { (void)kind; }
const char *http_last_error(void) { return NULL; }
void http_last_outcome(http_outcome_t *out) { memset(out, 0, sizeof(*out)); }
bool fixture_request(const char *url, const http_req_t *req, char **body, size_t *size, size_t limit, int timeout, int *status) {
	(void)url;
	(void)size;
	(void)limit;
	(void)timeout;
	calls++;
	assert(req->body && !strcmp(req->method, "POST"));
	assert(!strstr(req->body, "search") && !strstr(req->body, "stream"));
	if (strstr(req->body, "getBooksRecommendations")) {
		assert(strstr(req->body, children ? "CHILDREN_BOOK" : "\"BOOK\""));
		assert(strstr(req->body, "\"skip\":20") && strstr(req->body, "\"first\":3"));
	}
	*body = strdup(reply);
	*status = status_code;
	return true;
}

int main(void) {
	strcpy(token, "fixture");
	strcpy(base_url, "http://fixture");
	zvuk_album_t books[3];
	bool more = true;
	reply = "{\"data\":{\"recommendation\":{\"books\":[{\"id\":\"11\",\"title\":\"Book\",\"bookAuthors\":[{\"rname\":\"Author\"}],\"image\":{\"src\":\"https://example.invalid/cover\"}},null,{\"id\":\"12\",\"title\":\"Next page\"}]}}}";
	assert(zvuk_recommended_books(false, 20, books, 2, &more) == 1 && more);
	assert(!strcmp(books[0].id, "11") && !strcmp(books[0].artist, "Author") && books[0].cover[0]);
	children = true;
	assert(zvuk_recommended_books(true, 20, books, 2, &more) == 1 && more);
	reply = "{\"data\":{\"recommendation\":{\"books\":[]}}}";
	assert(zvuk_recommended_books(true, 20, books, 2, &more) == 0 && !more);
	reply = "{\"data\":{\"recommendation\":{\"books\":null}},\"errors\":[{\"message\":\"not available\"}]}";
	assert(zvuk_recommended_books(true, 20, books, 2, &more) == -1 && !more);
	int before = calls;
	assert(zvuk_recommended_books(false, -1, books, 2, &more) == -1 && calls == before);
	assert(zvuk_recommended_books(false, 20, books, 51, &more) == -1 && calls == before);
	zvuk_playlist_t lists[2];
	reply = "{\"data\":{\"dynamicBlockV2\":{\"items\":[{\"id\":\"3\",\"title\":\"Curated playlist\"},null]}}}";
	assert(zvuk_recommended_playlists(lists, 2) == 1 && !strcmp(lists[0].id, "3"));
	reply = "{\"data\":{\"dynamicBlockV2\":null}}";
	assert(zvuk_recommended_playlists(lists, 2) == -1);
	status_code = 500;
	assert(zvuk_recommended_playlists(lists, 2) == -1);
	puts("PASS Zvuk catalog: real recommendations, lookahead, children, pagination, empty/error, bounded requests");
	return 0;
}
