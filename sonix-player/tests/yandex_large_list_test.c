// cc -O1 -g -std=gnu11 -ffunction-sections -fdata-sections -I. -I./lvgl
// -DLV_CONF_INCLUDE_SIMPLE=1 tests/yandex_large_list_test.c
// src/system/streaming/yandexlist.c src/system/core/json.c
// -Wl,--gc-sections -pthread -o /tmp/yandex_large_list_test
#define yandex_tracks unused_network_tracks
#include "src/system/streaming/yandex.c"
#undef yandex_tracks
#include "src/system/streaming/yandexlist.h"
#include <assert.h>

static int requests;
int yandex_tracks(const char *const *ids, int count, yandex_track_t *out, int max) {
	assert(out && ids && count >= 0 && count <= YANDEXLIST_PAGE && max >= count);
	requests++;
	for (int i = 0; i < count; i++) {
		memset(&out[i], 0, sizeof(out[i]));
		size_t length = strcspn(ids[i], ":");
		assert(length < sizeof(out[i].id));
		memcpy(out[i].id, ids[i], length);
		out[i].id[length] = 0;
		char title[128];
		snprintf(title, sizeof(title), "Track %s", out[i].id);
		strcpy(out[i].title, title);
		out[i].available = true;
	}
	return count;
}

static void collect(const char *id, const char *album, const yandex_track_t *track, void *user) {
	yandexlist_t *list = user;
	assert(track ? yandexlist_add_track(list, track) : yandexlist_add_id(list, id, album));
}

int main(void) {
	for (int count = 5001; count <= 20000; count = 20000) {
		char *body = NULL;
		size_t size = 0;
		FILE *f = open_memstream(&body, &size);
		assert(f);
		fprintf(f, "{\"result\":{\"trackCount\":%d,\"tracks\":[", count);
		for (int i = 0; i < count; i++)
			fprintf(f, "%s{\"id\":%d,\"albumId\":123}", i ? "," : "", 100000 + i);
		fputs("]}}", f);
		assert(fclose(f) == 0 && size < YANDEX_BODY_LIMIT);
		json_doc_t doc;
		assert(json_parse(body, &doc));
		yandexlist_t *list = yandexlist_new();
		assert(list);
		int arr = json_path(&doc, json_root(&doc), "result.tracks");
		for (int el = next_element(&doc, arr, -1); el >= 0; el = next_element(&doc, arr, el))
			assert(ref_from(&doc, el, collect, list));
		assert(yandexlist_count(list) == count);
		json_free(&doc);
		free(body);
		char id[32], album[32], expected[32];
		for (int page = 0; page * YANDEXLIST_PAGE < count; page++) {
			assert(yandexlist_fetch_page(list, page) > 0);
			assert(yandexlist_memory(list) < 1024 * 1024);
		}
		yandexlist_t *copy = yandexlist_clone(list);
		assert(copy && yandexlist_count(copy) == count);
		assert(yandexlist_ids(copy, count - 1, id, sizeof(id), album, sizeof(album)));
		snprintf(expected, sizeof(expected), "%d", 100000 + count - 1);
		assert(!strcmp(id, expected) && !strcmp(album, "123"));
		assert(yandexlist_fetch_page(list, 0) > 0);
		printf("PASS Yandex %d refs: body %zu bytes, list %zu bytes, tail/clone/LRU intact\n", count, size, yandexlist_memory(list));
		yandexlist_free(copy);
		yandexlist_free(list);
		if (count == 20000)
			break;
	}
	assert(requests > 400);
	return 0;
}
