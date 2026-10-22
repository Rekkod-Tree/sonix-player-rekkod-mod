// Production remove/Undo callbacks, no GUI event/render or file writes.
// cc -O2 -std=gnu11 -ffunction-sections -fdata-sections
// -DLV_CONF_INCLUDE_SIMPLE -I. -Ilvgl tests/hometiles_undo_test.c
// -Wl,--gc-sections -o /tmp/home-undo-test
#include "src/gui/settings/hometiles.c"
#include <assert.h>
#include <string.h>
static home_item_t order[5] = {HOME_MUSIC, HOME_FILES, HOME_YANDEX, HOME_AUDIOBOOKS, HOME_SETTINGS};
static int count = 5;
static unsigned saves, errors;
static void (*pending_undo)(void *);
static void *pending_user;
int main_menu_count(void) { return count; }
home_item_t main_menu_at(int i) { return order[i]; }
bool main_menu_contains(home_item_t item) { return position_of(item) >= 0; }
bool main_menu_move(home_item_t item, int at) {
	int old = position_of(item);
	if (old < 0 && count == 5)
		return false;
	if (old >= 0) {
		memmove(order + old, order + old + 1, (count - old - 1) * sizeof(*order));
		count--;
	}
	if (at > count)
		at = count;
	memmove(order + at + 1, order + at, (count - at) * sizeof(*order));
	order[at] = item;
	count++;
	saves++;
	return true;
}
void main_menu_hide(home_item_t item) {
	int at = position_of(item);
	assert(at >= 0);
	memmove(order + at, order + at + 1, (count - at - 1) * sizeof(*order));
	count--;
	saves++;
}
const char *main_menu_tag(home_item_t item) {
	(void)item;
	return "shortcut";
}
const char *tr(const char *key) { return key; }
bool player_sheet_drag_active(void) { return false; }
bool switcher_back_drag_active(void) { return false; }
void sx_toast_undo(const char *text, void (*undo)(void *), void *user) {
	assert(strstr(text, "hometiles_remove"));
	pending_undo = undo;
	pending_user = user;
}
void toast_error(const char *text) {
	assert(!strcmp(text, "hometiles_full"));
	errors++;
}
lv_result_t lv_async_call(lv_async_cb_t cb, void *user) {
	(void)cb;
	(void)user;
	return LV_RESULT_OK;
}
int main(void) {
	remove_swipe(NULL, (void *)(intptr_t)HOME_FILES);
	assert(count == 4 && saves == 1 && !main_menu_contains(HOME_FILES));
	pending_undo(pending_user);
	assert(count == 5 && order[1] == HOME_FILES && saves == 2);
	pending_undo(pending_user);
	assert(saves == 2);
	remove_swipe(NULL, (void *)(intptr_t)HOME_FILES);
	main_menu_move(HOME_SETTINGS, 0);
	pending_undo(pending_user);
	assert(order[0] == HOME_SETTINGS && order[1] == HOME_FILES && order[2] == HOME_MUSIC);
	remove_swipe(NULL, (void *)(intptr_t)HOME_FILES);
	main_menu_move(HOME_NETWORKS, 0);
	pending_undo(pending_user);
	assert(count == 5 && order[0] == HOME_NETWORKS && !main_menu_contains(HOME_FILES) && errors == 1);
	remove_swipe(NULL, (void *)(intptr_t)HOME_MUSIC);
	main_menu_move(HOME_MUSIC, 2);
	unsigned before = saves;
	pending_undo(pending_user);
	assert(position_of(HOME_MUSIC) == 2 && saves == before);
	remove_swipe(NULL, (void *)(intptr_t)HOME_MUSIC);
	remove_swipe(NULL, (void *)(intptr_t)HOME_YANDEX);
	pending_undo(pending_user);
	assert(main_menu_contains(HOME_YANDEX) && !main_menu_contains(HOME_MUSIC));
	puts("PASS home remove/Undo: original index, intervening reorder retained, "
		 "full capacity guarded, readded ID untouched, latest Undo only");
}
