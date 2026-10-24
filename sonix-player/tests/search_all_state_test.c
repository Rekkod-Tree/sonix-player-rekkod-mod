// Production overview routing and asynchronous delivery, with inert UI sinks.
#include "src/gui/library/searchall.c"
#include <assert.h>
static unsigned fake_epoch = 5;
static bool finger;
static bool ready[SEARCH_SOURCE_MAX] = {true, true};
static bool ready0(void) { return ready[0]; }
static bool ready1(void) { return ready[1]; }
static bool ready2(void) { return ready[2]; }
static bool ready3(void) { return ready[3]; }
static bool ready4(void) { return ready[4]; }
static bool ready5(void) { return ready[5]; }
static const search_source_t delegates[] = {{.home = HOME_YANDEX, .name = "yandex", .kinds = 7, .ready = ready0}, {.home = HOME_ZVUK, .name = "zvuk", .kinds = 7, .ready = ready1}, {.home = HOME_TIDAL, .name = "tidal", .kinds = 7, .ready = ready2}, {.home = HOME_QOBUZ, .name = "qobuz", .kinds = 7, .ready = ready3}, {.home = HOME_SUBSONIC, .name = "subsonic", .kinds = 7, .ready = ready4}, {.home = HOME_PODCASTS, .name = "podcasts", .kinds = 8, .ready = ready5}};
int search_source_count(void) { return SEARCH_SOURCE_MAX; }
const search_source_t *search_source_at(int index) { return index >= 0 && index < SEARCH_SOURCE_MAX ? &delegates[index] : NULL; }
bool player_sheet_is_open(void) { return false; }
static int rendered, rebuilds;
void lv_obj_update_layout(const lv_obj_t *obj) { (void)obj; }
void lv_obj_scroll_to_view(lv_obj_t *obj, lv_anim_enable_t anim) { (void)obj; (void)anim; }
void lv_obj_set_hidden(lv_obj_t *obj, bool hidden) {
	(void)obj;
	(void)hidden;
}
void sx_pill_set_active(lv_obj_t *obj, bool active) {
	(void)obj;
	(void)active;
}
int settingsrow_content_top(gui_config_t *cfg) {
	(void)cfg;
	return 112;
}
void lv_obj_set_y(lv_obj_t *obj, int32_t y) {
	(void)obj;
	(void)y;
}
void lv_obj_set_height(lv_obj_t *obj, int32_t h) {
	(void)obj;
	(void)h;
}
unsigned library_database_epoch(void) { return fake_epoch; }
void library_index_close(library_index_t *ix) { (void)ix; }
int search_recent_queries(char out[][128], int max) {
	(void)out;
	(void)max;
	return 0;
}
lv_obj_t *lv_screen_active(void) { return screen; }
lv_indev_t *lv_indev_get_next(lv_indev_t *previous) { return finger && !previous ? (lv_indev_t *)(uintptr_t)1 : NULL; }
lv_indev_state_t lv_indev_get_state(const lv_indev_t *indev) {
	(void)indev;
	return finger ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
bool svclist_capture(const svclist_t *l, svclist_anchor_t *anchor) {
	(void)l;
	*anchor = (svclist_anchor_t){.valid = true};
	return true;
}
void svclist_set_count_at(svclist_t *l, int count, const svclist_anchor_t *anchor) {
	(void)l;
	(void)anchor;
	rendered = count;
	rebuilds++;
}
void svclist_set_count(svclist_t *l, int count, bool keep) {
	(void)l;
	(void)keep;
	rendered = count;
}

static response_t *answer(unsigned serial, unsigned epoch, int source, int count) {
	response_t *r = calloc(1, sizeof(*r));
	assert(r);
	r->serial = serial;
	r->epoch = epoch;
	r->source = source;
	r->count = count;
	for (int i = 0; i < count; i++)
		r->hits[i].kind = i % 3;
	return r;
}
int main(void) {
	screen = (lv_obj_t *)(uintptr_t)1;
	list = (svclist_t *)(uintptr_t)2;
	snprintf(searched, sizeof(searched), "A query");
	query_epoch = fake_epoch;
	included[0] = included[1] = true;
	local_count[0] = 9000;
	local_count[1] = 1;
	local_count[2] = 0;
	assert(total_items() == 6); // 2 card tracks + More + 1 album + 2 services.
	int source, kind, at;
	assert(!item_at(-1, &source, &kind, &at));
	assert(item_at(2, &source, &kind, &at) == 2 && !source && !kind);
	assert(item_at(3, &source, &kind, &at) == 1 && !source && kind == 1);
	assert(item_at(4, &source, &kind, &at) == 2 && source == 1);
	assert(item_at(5, &source, &kind, &at) == 2 && source == 2);
	assert(!item_at(6, &source, &kind, &at));
	generation = 10;
	accept_response(answer(9, fake_epoch, 0, 3));
	assert(!remote[0] && !rebuilds);
	accept_response(answer(10, fake_epoch - 1, 0, 3));
	assert(!remote[0] && !rebuilds);
	accept_response(answer(10, fake_epoch, 0, 3));
	assert(remote[0] && status[0] == 2 && rendered == 9);
	finger = true;
	accept_response(answer(10, fake_epoch, 1, 4));
	assert(!remote[1] && deferred[1] && rendered == 9);
	finger = false;
	searchall_poll();
	assert(remote[1] && !deferred[1] && rendered == 13);
	response_t *partial = answer(10, fake_epoch, 1, 2);
	partial->failed = true;
	accept_response(partial);
	assert(remote[1]->count == 2 && status[1] == 3 && rendered == 11);
	response_t *lost = answer(10, fake_epoch, 1, 0);
	lost->failed = true;
	accept_response(lost);
	assert(remote[1]->count == 2 && status[1] == 3); // Same-query retry failure keeps usable partial data.
	const int counts[] = {7, 5, 3};
	for (int filter = 0; filter < 3; filter++) {
		type_filter = filter;
		assert(total_items() == counts[filter]);
		for (int i = 0; i < total_items(); i++) {
			assert(item_at(i, &source, &kind, &at));
			assert(kind == filter);
			if (source > 0 && item_at(i, &source, &kind, &at) == 1)
				assert(remote[source - 1]->hits[at].kind == filter);
		}
	}
	type_filter = -1;
	editing = true;
	recent_count = 1;
	assert(total_items() == 1 && item_at(0, &source, &kind, &at) == 1 && source == -1);
	editing = false;
	assert(total_items() == 11);
	request_t request = {.serial = 10};
	report_delivery_failure(&request, 0);
	searchall_poll();
	assert(status[0] == 3);
	ready[1] = false;
	searchall_poll();
	assert(!remote[1] && !status[1]);
	accept_response(answer(10, fake_epoch, 1, 3));
	assert(!remote[1] && !status[1]); // A response after logout stays unavailable.
	ready[1] = true;
	cancel_request();
	accept_response(answer(10, fake_epoch, 0, 6));
	assert(remote[0]->count == 3);
	fake_epoch++;
	searchall_poll();
	assert(!remote[0] && !remote[1] && !searched[0]);
	snprintf(recents[0], sizeof(recents[0]), "Recent query");
	recent_count = 1;
	assert(total_items() == 1 && item_at(0, &source, &kind, &at) == 1 && source == -1);
	clear_results();
	// All six sources own independent snapshots; podcast types cannot leak into
	// music filters, and every nested payload is released on invalidation.
	editing = false;
	snprintf(searched, sizeof(searched), "query");
	query_epoch = fake_epoch;
	generation = 21;
	for (int i = 0; i < SEARCH_SOURCE_MAX; i++) {
		ready[i] = included[i] = true;
		response_t *r = answer(21, fake_epoch, i, 1);
		r->hits[0].kind = i == 5 ? SEARCH_PODCAST : SEARCH_TRACK;
		r->hits[0].data = malloc(17);
		accept_response(r);
		assert(remote[i] && remote[i]->count == 1);
	}
	type_filter = SEARCH_PODCAST;
	assert(total_items() == 2 && item_at(0, &source, &kind, &at) == 1 && source == 6 && kind == SEARCH_PODCAST);
	type_filter = SEARCH_ARTIST;
	assert(total_items() == 5); // Music sources only, even if Podcast Index is ready.
	ready[4] = false;
	searchall_poll();
	assert(!remote[4] && status[4] == 0);
	accept_response(answer(21, fake_epoch, 4, 1));
	assert(!remote[4]);
	clear_results();
	printf("PASS grouped overview, 9000 local hits without copied rows, stale query/card rejection, held-row deferral, partial service results and delivery failure; snapshot headers <= %zu bytes (native payloads separate)\n", sizeof(response_t) * SEARCH_SOURCE_MAX);
	return 0;
}
