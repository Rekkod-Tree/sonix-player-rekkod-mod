#define _GNU_SOURCE 1
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
void *fixture_calloc(size_t, size_t);
FILE *fixture_fopen(const char *, const char *);
#define calloc fixture_calloc
#define fopen fixture_fopen
#include "src/system/library/playlists.c"
#undef calloc
#undef fopen
int fixture_rows_from_file(const char *name, const char *path) {
	return rows_from_file(name, path);
}
