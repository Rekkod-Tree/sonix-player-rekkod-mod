#include "historypage.h"

#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include "src/gui/nowplaying/trackmenu.h"
#include "src/gui/shell/settingsrow.h"
#include "src/gui/shell/switcher.h"
#include "src/gui/shell/sx_frame.h"
#include "src/gui/shell/sx_palette.h"
#include "src/gui/shell/sx_row.h"
#include "src/gui/shell/sx_tokens.h"
#include "src/gui/shell/theme.h"
#include "src/system/core/lang.h"
#include "src/system/library/library.h"

#define POOL 16
static lv_obj_t *screen, *viewport, *body, *empty;
static lv_obj_t *rows[POOL];
static library_history_item_t *items;
static int count, pitch;
static int bound[POOL];

static void queue_next(lv_obj_t *row, void *user) {
	(void)row;
	int at = (int)(intptr_t)user;
	char path[512], name[256];
	if (at < 0 || at >= count || !library_history_entry(items[count - 1 - at].id, path, sizeof(path), name, sizeof(name), NULL, 0)) {
		return;
	}
	const char *paths[] = {path};
	trackmenu_queue_paths(paths, 1, PLAYLIST_QUEUE_NEXT);
}

static void window_update(lv_event_t *e) {
	(void)e;
	int first = lv_obj_get_scroll_y(viewport) / pitch;
	if (first < 0) {
		first = 0;
	}
	for (int i = 0; i < POOL; i++) {
		int at = first + i;
		lv_obj_set_hidden(rows[i], at >= count);
		if (at >= count || bound[i] == at) {
			continue;
		}
		bound[i] = at;
		char path[512] = "", title[256], artist[256] = "";
		library_history_item_t *item = &items[count - 1 - at];
		if (!library_history_entry(item->id, path, sizeof(path), title, sizeof(title), artist, sizeof(artist))) {
			title[0] = '\0';
		}
		char when[48] = "";
		if (item->played_at > 0) {
			time_t stamp = (time_t)item->played_at;
			struct tm tm;
			localtime_r(&stamp, &tm);
			strftime(when, sizeof(when), "%d.%m %H:%M", &tm);
		}
		sx_row_set_user(rows[i], (void *)(intptr_t)at);
		sx_row_set_title(rows[i], title);
		sx_row_set_subtitle(rows[i], artist, NULL, when);
		sx_row_set_thumb(rows[i], NULL, sx_placeholder_name(title));
		lv_obj_set_pos(rows[i], 0, at * pitch);
	}
}

static void unloaded(lv_event_t *e) {
	(void)e;
	free(items);
	items = NULL;
	count = 0;
}

void historypage_open(void *unused) {
	gui_page_ensure(screen);
	(void)unused;
	free(items);
	items = NULL;
	count = library_history_items(&items);
	pitch = sx_row_height(true) + SX_ROW_GAP;
	for (int i = 0; i < POOL; i++) {
		bound[i] = -1;
		sx_row_set_compact(rows[i], true);
	}
	lv_obj_set_height(body, count ? count * pitch : 1);
	lv_obj_set_hidden(empty, count != 0);
	lv_obj_scroll_to_y(viewport, 0, LV_ANIM_OFF);
	switch_screen(screen);
	window_update(NULL);
}

static void build_page(gui_config_t *cfg) {
	lv_obj_add_style(screen, &theme_style_screen, 0);
	settingsrow_title(screen, cfg, "queue_history");
	viewport = lv_obj_create(screen);
	lv_obj_remove_style_all(viewport);
	lv_obj_set_size(viewport, cfg->screen_width - SX_BODY_PAD_L - SX_BODY_PAD_R, cfg->screen_height - settingsrow_content_top(cfg));
	lv_obj_set_pos(viewport, SX_BODY_PAD_L, settingsrow_content_top(cfg));
	lv_obj_set_scroll_dir(viewport, LV_DIR_VER);
	sx_frame_reserve_bottom(viewport);
	body = lv_obj_create(viewport);
	lv_obj_remove_style_all(body);
	lv_obj_set_width(body, lv_pct(100));
	lv_obj_set_scrollable(body, false);
	const sx_row_handlers_t handlers = {.on_tap = queue_next, .on_swipe_next = queue_next};
	for (int i = 0; i < POOL; i++) {
		rows[i] = sx_row_create(body, &handlers, true);
		lv_obj_set_width(rows[i], lv_pct(100));
		lv_obj_set_hidden(rows[i], true);
	}
	empty = lv_label_create(viewport);
	lv_label_set_text(empty, tr("queue_history_empty"));
	lv_obj_add_style(empty, &theme_style_text_dim, 0);
	lv_obj_center(empty);
	lv_obj_add_event_cb(viewport, window_update, LV_EVENT_SCROLL, NULL);
	lv_obj_add_event_cb(screen, unloaded, LV_EVENT_SCREEN_UNLOADED, NULL);
	switcher_attach_back_gesture(screen);
}

void historypage_init(gui_config_t *cfg) {
	(void)cfg;
	screen = lv_obj_create(NULL);
	gui_page_defer(screen, build_page);
}
