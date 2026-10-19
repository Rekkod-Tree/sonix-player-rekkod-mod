/* cc -std=gnu11 -O1 -g -Wall -Wextra -ffunction-sections -fdata-sections
 * -fsanitize=address,undefined -I. tests/yandex_catalog_test.c
 * src/system/core/json.c -Wl,--gc-sections -pthread -o /tmp/yandex-catalog-test
 */
#include "src/system/streaming/yandex.c"
#include <assert.h>
const char *tr(const char *key) { return key; }

static int parse(const char *json, const char *route, const char *section, yandex_catalog_item_t *items, int max) {
	json_doc_t doc;
	assert(json_parse(json, &doc));
	int count = catalog_from(&doc, route, section, items, max);
	json_free(&doc);
	return count;
}

int main(int argc, char **argv) {
	yandex_catalog_item_t items[150];
	const char *normal = "{\"result\":{\"blocks\":["
						 "{\"id\":\"chart\",\"title\":\"Book chart\",\"type\":\"editorial-playlists\",\"entities\":["
						 "{\"type\":\"album\",\"data\":{\"id\":1,\"title\":\"Book\",\"type\":\"audiobook\",\"metaType\":\"podcast\"}},"
						 "{\"type\":\"album\",\"data\":{\"id\":2,\"title\":\"Show\",\"type\":\"podcast\"}},"
						 "{\"type\":\"album\",\"data\":{\"id\":3,\"title\":\"Unavailable\",\"type\":\"audiobook\",\"available\":false}},"
						 "{\"type\":\"album\",\"data\":{\"id\":4,\"title\":\"Unknown\",\"metaType\":\"podcast\"}}]},"
						 "{\"id\":\"genres\",\"type\":\"categories-tab\",\"entities\":["
						 "{\"type\":\"category\",\"data\":{\"categoryId\":\"fiction\",\"title\":\"Invisible\",\"voiceTitle\":\"Fiction\"}},"
						 "{\"type\":\"category\",\"data\":{\"categoryId\":\"../bad\",\"title\":\"Bad\"}}]},"
						 "{\"id\":\"ad\",\"type\":\"promotions\",\"entities\":[{\"type\":\"promotion\",\"data\":{\"url\":\"https://example.invalid\"}}]}]}}";
	assert(parse(normal, "books", "", items, 150) == 2);
	assert(items[0].kind == YANDEX_CATALOG_CATEGORY && !strcmp(items[0].u.category.path, "books#chart"));
	assert(!strcmp(items[1].u.category.title, "Fiction") && !strcmp(items[1].u.category.path, "category/fiction"));
	assert(parse(normal, "books", "chart", items, 150) == 1);
	assert(items[0].kind == YANDEX_CATALOG_ALBUM && !strcmp(items[0].u.album.id, "1"));
	assert(parse(normal, "books", "missing", items, 150) == 0);
	assert(parse(normal, "picks", "", items, 150) == 0);
	memset(&items[1], 0x55, sizeof(items[1]));
	assert(parse(normal, "books", "", items, 1) == 1 && ((unsigned char *)&items[1])[0] == 0x55);
	assert(parse("{\"result\":{\"blocks\":[]}}", "books", "", items, 150) == 0);
	assert(parse("{\"result\":null}", "books", "", items, 150) == -1);
	assert(!catalog_id_valid("a/b") && !catalog_id_valid("a?b") && !catalog_id_valid("") && catalog_id_valid("AUDIOBOOKS_MENU"));
	// Optional local live-response replay; no account state is committed here.
	if (argc == 3) {
		FILE *f = fopen(argv[1], "rb");
		assert(f);
		char *text = calloc(4 * 1024 * 1024 + 1, 1);
		assert(text);
		assert(fread(text, 1, 4 * 1024 * 1024, f) > 0 && !ferror(f));
		fclose(f);
		int count = parse(text, argv[2], "", items, 150);
		assert(count > 0);
		if (!strcmp(argv[2], "picks"))
			for (int i = 0; i < count; i++)
				assert(items[i].kind == YANDEX_CATALOG_PLAYLIST);
		printf("live response: %d entries\n", count);
		free(text);
	}
	puts("PASS Yandex catalog: categories, real book types, capacity, empty/error, safe routes");
	return 0;
}
