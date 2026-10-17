// The actual GUI search router, without starting a GUI or network worker.
#include "src/gui/streaming/zvukpage.c"
#include <assert.h>
int main(void) {
	job_kind_t kind = JOB_NONE;
	char id[ZVUK_ID_MAX];
	assert(spoken_link("https://zvuk.com/abook/28850372", &kind, id, sizeof(id)));
	assert(kind == JOB_BOOK && !strcmp(id, "28850372"));
	assert(spoken_link("https://zvuk.com/podcast/12874277?source=share", &kind, id, sizeof(id)));
	assert(kind == JOB_PODCAST && !strcmp(id, "12874277"));
	assert(!spoken_link("Normal song search", &kind, id, sizeof(id)));
	assert(!spoken_link("https://zvuk.com/release/123", &kind, id, sizeof(id)));
	assert(!spoken_link("https://example.org/podcast/12874277", &kind, id, sizeof(id)));
	assert(!spoken_link("https://zvuk.com/podcast/", &kind, id, sizeof(id)));
	puts("PASS: official book/podcast links, tracking query, unchanged plain music search");
	return 0;
}
