// Production menu/entry code with explicit DB/player/UI sinks; no device/files.
// cc -O1 -g -std=gnu11 -ffunction-sections -fdata-sections
// -DLV_CONF_INCLUDE_SIMPLE -I. -Ilvgl tests/bookresume_checkpoint_test.c
// -Wl,--gc-sections -lm -o /tmp/bookresume-test
#define access fixture_access
#include "src/gui/shell/bookresume.c"
#undef access
#include <assert.h>

lv_obj_t *audiobooks_screen;
const lv_image_dsc_t icon_menu_audiobook_continue = {0}, icon_menu_audiobook_library = {0}, icon_menu_audiobooks = {0}, icon_menu_more = {0}, icon_menu_settings = {0};
static char selected[512] = "/books/Book A", saved_file[512] = "/books/Book A/Chapter 07.m4a";
static char current_book[512], current_file[512], called_book[512], header[1024];
static bool card = true, saved = true, available = true, finished;
static double saved_seconds = 3735;
static device_state_t current;
static unsigned serial = 1, epoch = 1, menus, plays, pauses, errors, navigations, writes;
static const char *last_error;
static sx_sheet_item_t shown[5];

const char *tr(const char *key) { return key; }
const char *config_get_copy(const char *section, const char *key, const char *fallback, char *out, size_t size) {
	(void)section;
	const char *value = !strcmp(key, "resume_book") ? selected : !strcmp(key, "resume_title") ? "Book A" : fallback;
	snprintf(out, size, "%s", value);
	return out;
}
bool gui_card_available(void) { return card; }
bool audiobookdb_is_finished(const char *book) {
	assert(book[0]);
	return finished;
}
bool audiobookdb_get_position(const char *book, char *file, size_t size, double *seconds) {
	assert(book[0]);
	snprintf(file, size, "%s", saved_file);
	*seconds = saved_seconds;
	return saved;
}
bool audiobook_current_book(char *book, size_t bs, char *file, size_t fs) {
	snprintf(book, bs, "%s", current_book);
	snprintf(file, fs, "%s", current_file);
	return current_book[0] != 0;
}
unsigned audiobook_serial(void) { return serial; }
unsigned audiobookdb_card_epoch(void) { return epoch; }
void audiobook_place_label(double seconds, char *out, size_t size) {
	assert(seconds == current.progress_current_secs);
	snprintf(out, size, "Loaded chapter");
}
void device_state_get(device_state_t *state) { *state = current; }
int fixture_access(const char *file, int mode) {
	(void)mode;
	assert(file[0]);
	return available ? 0 : -1;
}
void audiobooks_resume_book(const char *book) {
	plays++;
	snprintf(called_book, sizeof(called_book), "%s", book);
}
void player_key_play_pause(void) { pauses++; }
void toast_error(const char *key) {
	errors++;
	last_error = key;
}
void switch_screen(lv_obj_t *screen) {
	(void)screen;
	navigations++;
}
void audiobooks_open_continue(void) { navigations++; }
bool main_menu_contains(home_item_t item) {
	(void)item;
	return true;
}
bool main_menu_move(home_item_t item, int at) {
	(void)item;
	(void)at;
	return true;
}
void main_menu_hide(home_item_t item) {
	(void)item;
	writes++;
}
int main_menu_count(void) { return 5; }
void main_menu_open_settings(void) { navigations++; }
void sx_sheet_menu(const char *title, const char *subtitle, const sx_sheet_item_t *items, int n) {
	assert(!strcmp(title, "home_resume_book") && n <= 5);
	menus++;
	snprintf(header, sizeof(header), "%s", subtitle);
	memcpy(shown, items, n * sizeof(*items));
}
int main(void) {
	bookresume_menu();
	assert(strstr(header, "Book A\nChapter 07.m4a · 1:02:15"));
	assert(plays == 0 && pauses == 0 && writes == 0);
	strcpy(selected, "/books/Book B");
	shown[0].action(shown[0].user);
	assert(plays == 1 && !strcmp(called_book, "/books/Book A"));
	sx_sheet_item_t stale_action = shown[0];
	bookresume_menu();
	stale_action.action(stale_action.user);
	assert(plays == 1);
	epoch++;
	shown[0].action(shown[0].user);
	assert(plays == 1 && !strcmp(last_error, "home_card_unavailable"));
	strcpy(selected, "/books/Book A");
	strcpy(current_book, selected);
	strcpy(current_file, saved_file);
	strcpy(current.current_file, current_file);
	current.status = AUDIO_STATUS_PAUSED;
	current.progress_current_secs = 125;
	bookresume_menu();
	assert(strstr(header, "Loaded chapter · 2:05"));
	unsigned before = menus;
	bookresume_play();
	assert(pauses == 1 && menus == before && plays == 1);
	current.status = AUDIO_STATUS_PLAYING;
	bookresume_play();
	assert(pauses == 1 && plays == 1 && menus == before);
	current_book[0] = current_file[0] = 0;
	finished = true;
	bookresume_play();
	assert(plays == 2 && menus == before);
	bookresume_menu();
	assert(strstr(header, "audiobook_finished"));
	card = false;
	before = plays;
	bookresume_play();
	assert(plays == before && !strcmp(last_error, "home_card_unavailable"));
	bookresume_menu();
	assert(strstr(header, "Book A\nhome_card_unavailable"));
	card = true;
	available = false;
	bookresume_play();
	assert(plays == before && !strcmp(last_error, "home_book_unavailable"));
	bookresume_menu();
	assert(strstr(header, "home_book_unavailable"));
	available = true;
	saved = false;
	bookresume_play();
	assert(plays == before);
	saved = true;
	saved_seconds = -1;
	bookresume_menu();
	assert(strstr(header, "0:00"));
	saved_seconds = NAN;
	bookresume_menu();
	assert(strstr(header, "0:00"));
	selected[0] = 0;
	bookresume_play();
	assert(navigations == 1 && writes == 0 && errors == 4);
	puts("PASS bookresume: saved/live checkpoint, pinned menu target, direct tap "
		 "playing/paused/finished, unavailable/no checkpoint, invalid time; no "
		 "writes");
}
