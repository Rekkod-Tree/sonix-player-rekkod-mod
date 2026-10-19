#include "bookresume.h"
#include "icons.h"
#include "main_menu.h"
#include "src/gui/library/audiobooks.h"
#include "src/gui/nowplaying/player.h"
#include "src/system/core/config.h"
#include "src/system/core/lang.h"
#include "src/system/library/audiobookdb.h"
#include "src/system/playback/audiobook.h"
#include "switcher.h"
#include "sx_sheet.h"
#include "toast.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Only the GUI thread reads and updates the home selection. No new timer.
static char checked_file[512];
static unsigned checked_serial;
static char menu_book[512];
static char menu_title[256];

static void selected_book(char *book, size_t size) {
	size_t used = 0;
	if (!size)
		return;
	book[0] = 0;
	for (int i = 0; i < 5; i++) {
		char key[32];
		snprintf(key, sizeof(key), i ? "resume_book_%d" : "resume_book", i);
		const char *part = config_get("home", key, "");
		size_t length = strlen(part);
		if (used + length >= size) {
			book[0] = 0;
			return;
		}
		memcpy(book + used, part, length + 1);
		used += length;
	}
}
static void select_book(const char *book, const char *title) {
	// Config values hold 127 bytes. Split at UTF-8 boundaries, not mid-path.
	for (int i = 0; i < 5; i++) {
		char key[32], part[121];
		snprintf(key, sizeof(key), i ? "resume_book_%d" : "resume_book", i);
		size_t n = strlen(book);
		if (n > 120) {
			n = 120;
			while (n && ((unsigned char)book[n] & 0xc0) == 0x80)
				n--;
		}
		memcpy(part, book, n);
		part[n] = 0;
		config_set("home", key, part);
		book += n;
	}
	char short_title[128];
	size_t n = strlen(title);
	if (n > 127) {
		n = 127;
		while (n && ((unsigned char)title[n] & 0xc0) == 0x80)
			n--;
	}
	memcpy(short_title, title, n);
	short_title[n] = 0;
	config_set("home", "resume_title", short_title);
	config_save();
}

void bookresume_note(const device_state_t *state) {
	if (state->status != AUDIO_STATUS_PLAYING || state->live || !state->current_file[0])
		return;
	unsigned serial = audiobook_serial();
	if (!strcmp(checked_file, state->current_file) && serial == checked_serial)
		return;
	snprintf(checked_file, sizeof(checked_file), "%s", state->current_file);
	checked_serial = serial;
	char book[512], file[512], title[256];
	if (!audiobook_current_book(book, sizeof(book), file, sizeof(file)) || strcmp(file, state->current_file))
		return;
	char selected[512];
	selected_book(selected, sizeof(selected));
	if (!strcmp(book, selected))
		return;
	if (!audiobookdb_book_info(book, title, sizeof(title), NULL, 0, NULL))
		return;
	select_book(book, title);
	main_menu_refresh();
}
void bookresume_description(char *text, size_t size) {
	const char *title = config_get("home", "resume_title", "");
	snprintf(text, size, "%s", title[0] ? title : tr("home_choose_book"));
}
static void play(const char *book, bool confirmed) {
	if (!book[0]) {
		switch_screen(audiobooks_screen);
		return;
	}
	if (!gui_card_available()) {
		toast_error("home_card_unavailable");
		return;
	}
	if (!confirmed && audiobookdb_is_finished(book)) {
		// A finished selection needs an explicit choice, not an error toast.
		bookresume_menu();
		return;
	}
	char current[512], file[512];
	if (audiobook_current_book(current, sizeof(current), file, sizeof(file)) && !strcmp(current, book)) {
		device_state_t state;
		device_state_get(&state);
		if (!strcmp(state.current_file, file) && state.status == AUDIO_STATUS_PLAYING)
			return;
		if (!strcmp(state.current_file, file) && state.status == AUDIO_STATUS_PAUSED) {
			player_key_play_pause();
			return;
		}
	}
	// A removed checkpoint must not silently turn Continue into Start over.
	double seconds;
	if (!audiobookdb_get_position(book, file, sizeof(file), &seconds) || access(file, R_OK)) {
		toast_error("home_book_unavailable");
		return;
	}
	audiobooks_resume_book(book);
}
void bookresume_play(void) {
	char book[512];
	selected_book(book, sizeof(book));
	play(book, false);
}
static void continue_action(void *unused) {
	(void)unused;
	play(menu_book, true);
}
static void other_action(void *unused) {
	(void)unused;
	audiobooks_open_continue();
}
static void library_action(void *unused) {
	(void)unused;
	switch_screen(audiobooks_screen);
}
static void move_action(void *unused) {
	(void)unused;
	if (main_menu_contains(HOME_CONTINUE_BOOK))
		main_menu_hide(HOME_CONTINUE_BOOK);
	else if (!main_menu_move(HOME_CONTINUE_BOOK, main_menu_count()))
		toast_error("home_full");
}
static void settings_action(void *unused) {
	(void)unused;
	main_menu_open_settings();
}
void bookresume_menu(void) {
	selected_book(menu_book, sizeof(menu_book));
	snprintf(menu_title, sizeof(menu_title), "%s", config_get("home", "resume_title", ""));
	sx_sheet_item_t items[5];
	int n = 0;
	if (menu_book[0])
		items[n++] = (sx_sheet_item_t){.label = tr("home_resume_book"), .icon = &icon_menu_audiobook_continue, .action = continue_action};
	items[n++] = (sx_sheet_item_t){.label = tr("home_other_books"), .icon = &icon_menu_audiobook_library, .action = other_action};
	items[n++] = (sx_sheet_item_t){.label = tr("audiobooks"), .icon = &icon_menu_audiobooks, .action = library_action};
	items[n++] = (sx_sheet_item_t){.label = tr(main_menu_contains(HOME_CONTINUE_BOOK) ? "home_to_more" : "home_to_main"), .icon = &icon_menu_more, .action = move_action};
	items[n++] = (sx_sheet_item_t){.label = tr("home_settings"), .icon = &icon_menu_settings, .action = settings_action};
	sx_sheet_menu(tr("home_resume_book"), menu_title, items, n);
}
