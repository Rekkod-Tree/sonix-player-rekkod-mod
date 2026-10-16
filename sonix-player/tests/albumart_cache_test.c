/* cc -std=gnu11 -O1 -Wall -Wextra -ffunction-sections -fdata-sections -I.
 * tests/albumart_cache_test.c -Wl,--gc-sections -pthread -o /tmp/art-test
 * /tmp/art-test
 * Real directory-selection code, ordinary temporary files; no player/SD.
 */
#define _GNU_SOURCE
#include <assert.h>
#include "src/system/library/albumart.c"

bool has_extension(const char *name, const char *extension) {
	size_t n = strlen(name), e = strlen(extension);
	return n >= e && strcasecmp(name + n - e, extension) == 0;
}

static void make_file(const char *dir, const char *name) {
	char path[600];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	FILE *f = fopen(path, "w");
	assert(f && fclose(f) == 0);
}
static void remove_file(const char *dir, const char *name) {
	char path[600];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	assert(unlink(path) == 0);
}
int main(void) {
	char root[] = "/tmp/sonix-art-XXXXXX", local[512], dir[512], out[600];
	assert(mkdtemp(root));
	snprintf(local, sizeof(local), "%s/.local", root);
	assert(mkdir(local, 0700) == 0);
	const char *names[] = {"yandex-cache", "zvuk-cache", "ordinary-album"};
	for (int i = 0; i < 3; i++) {
		snprintf(dir, sizeof(dir), "%s/.local/%s", root, names[i]);
		assert(mkdir(dir, 0700) == 0);
		make_file(dir, "A.jpg");
		assert(find_cover_file(dir, "A", out, sizeof(out)));
		assert(find_cover_file(dir, "B", out, sizeof(out)) == (i == 2));
		make_file(dir, "cover.jpg");
		assert(find_cover_file(dir, "B", out, sizeof(out)) == (i == 2));
		make_file(dir, "B.PNG");
		assert(find_cover_file(dir, "B", out, sizeof(out)));
		assert(strstr(out, "/B.PNG"));
		remove_file(dir, "A.jpg");
		remove_file(dir, "B.PNG");
		remove_file(dir, "cover.jpg");
		assert(rmdir(dir) == 0);
	}
	assert(rmdir(local) == 0 && rmdir(root) == 0);
	puts("albumart cache isolation: PASS");
}
