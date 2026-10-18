#include "main_menu.h"
#include "bookresume.h"
#include "icons.h"
#include "morepage.h"
#include "src/gui/library/audiobooks.h"
#include "src/gui/library/filespage.h"
#include "src/gui/library/music.h"
#include "src/gui/library/search.h"
#include "src/gui/library/shufflecard.h"
#include "src/gui/nowplaying/coverflow.h"
#include "src/gui/nowplaying/player.h"
#include "src/gui/settings/hometiles.h"
#include "src/gui/settings/settings.h"
#include "src/gui/streaming/streaming.h"
#include "src/gui/streaming/yandexpage.h"
#include "switcher.h"
#include "sx_sheet.h"
#include "toast.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef SONIX_ZVUK
#include "src/gui/streaming/zvukpage.h"
#endif
#include "src/gui/wireless/wireless.h"
#include "src/system/bluetooth/bluetooth.h"
#include "src/system/core/config.h"
#include "src/system/core/lang.h"
#include "src/system/library/audiobookdb.h"
#include "src/system/net/wifi.h"

lv_obj_t *main_menu_screen;
static gui_config_t *page_cfg;
static lv_obj_t *tile_grid;
static home_item_t order[HOME_CUSTOM_SLOTS];
static int order_n;
static bool loaded;
static const char *ids[] = {"music", "streaming", "networks", "audiobooks", "settings", "continue_book", "tidal", "qobuz", "radio", "podcasts", "subsonic", "yandex", "zvuk", "music_all", "music_albums", "music_artists", "music_album_artists", "music_genres", "music_years", "music_favourites", "music_most_played", "music_recent_added", "music_recent_played", "music_playlists", "music_browse", "search", "files"};
static const char *tags[] = {"music", "streaming", "wireless", "audiobooks", "settings", "home_resume_book", "tidal", "qobuz", "radio", "podcasts", "subsonic", "yandex", "zvuk", "music_all_tracks", "albums", "artists", "music_album_artists", "music_genres", "music_year", "favourites", "music_most_played", "music_recently_added", "music_recently_played", "playlists", "music_browse", "home_search", "file_explorer"};
static const lv_image_dsc_t *icons[] = {&icon_menu_music, &icon_menu_streaming, &icon_menu_wireless, &icon_menu_audiobooks, &icon_menu_settings, &icon_menu_audiobooks};

_Static_assert(sizeof(ids) / sizeof(ids[0]) == HOME_ITEM_COUNT, "Home IDs must match items");
_Static_assert(sizeof(tags) / sizeof(tags[0]) == HOME_ITEM_COUNT, "Home tags must match items");
_Static_assert(HOME_MUSIC_BROWSE - HOME_MUSIC_ALL + 1 == MUSIC_TILE_COUNT, "Music shortcuts must match tiles");

static bool valid(home_item_t item) { return item >= 0 && item < HOME_ITEM_COUNT; }
static bool contains(home_item_t item) {
	for (int i = 0; i < order_n; i++)
		if (order[i] == item)
			return true;
	return false;
}
static void defaults(void) {
	order_n = HOME_CUSTOM_SLOTS;
	for (int i = 0; i < order_n; i++)
		order[i] = (home_item_t)i;
}
static void load(void) {
	if (loaded)
		return;
	loaded = true;
	const char *saved = config_get("home", "tiles", NULL);
	if (!saved) {
		defaults();
		return;
	}
	order_n = 0;
	while (*saved && order_n < HOME_CUSTOM_SLOTS) {
		const char *end = strchr(saved, ',');
		size_t length = end ? (size_t)(end - saved) : strlen(saved);
		for (int i = 0; i < HOME_ITEM_COUNT; i++) {
			if (strlen(ids[i]) == length && !strncmp(saved, ids[i], length) && !contains((home_item_t)i) && main_menu_available((home_item_t)i))
				order[order_n++] = (home_item_t)i;
		}
		if (!end)
			break;
		saved = end + 1;
	}
}
int main_menu_count(void) {
	load();
	return order_n;
}
home_item_t main_menu_at(int pos) {
	load();
	return pos >= 0 && pos < order_n ? order[pos] : HOME_ITEM_COUNT;
}
bool main_menu_contains(home_item_t item) {
	load();
	return contains(item);
}
bool main_menu_available(home_item_t item) { return valid(item) && (item <= HOME_CONTINUE_BOOK || item >= HOME_MUSIC_ALL || streaming_home_entry(ids[item], NULL)); }
const char *main_menu_tag(home_item_t item) { return valid(item) ? tags[item] : "more"; }
const lv_image_dsc_t *main_menu_icon(home_item_t item) {
	grid_entry_t entry;
	if (item >= HOME_MUSIC_ALL && item <= HOME_MUSIC_BROWSE && music_home_entry((music_tile_t)(item - HOME_MUSIC_ALL), &entry))
		return entry.icon;
	if (item == HOME_SEARCH)
		return &icon_search;
	if (item == HOME_FILES)
		return &icon_menu_file_explorer;
	if (item > HOME_CONTINUE_BOOK && item <= HOME_ZVUK && streaming_home_entry(ids[item], &entry))
		return entry.icon;
	return item >= 0 && item <= HOME_CONTINUE_BOOK ? icons[item] : &icon_menu_more;
}

static void music_description(char *text, size_t size) {
	if (!gui_card_available())
		snprintf(text, size, "%s", tr("home_card_unavailable"));
	else
		shufflecard_tracks_description(text, size);
}
static void books_description(char *text, size_t size) {
	if (!gui_card_available())
		snprintf(text, size, "%s", tr("home_card_unavailable"));
	else if (audiobookdb_count() > 0)
		snprintf(text, size, tr("home_books_n"), audiobookdb_count());
}
static void network_description(char *text, size_t size) {
	wifi_status_t wifi;
	wifi_get_status(&wifi);
	bt_device_t device;
	const char *tag = wifi.state == WIFI_STATE_CONNECTED ? "home_wifi_connected" : bluetooth_connected_device(&device) ? "home_bt_connected" : "home_no_connections";
	snprintf(text, size, "%s", tr(tag));
}
bool main_menu_get_entry(home_item_t item, grid_entry_t *out) {
	if (!valid(item) || !out)
		return false;
	if (item >= HOME_MUSIC_ALL && item <= HOME_MUSIC_BROWSE)
		return music_home_entry((music_tile_t)(item - HOME_MUSIC_ALL), out);
	if (item == HOME_SEARCH) {
		*out = (grid_entry_t){"home_search", &icon_search, NULL, main_menu_open_search};
		return true;
	}
	if (item == HOME_FILES) {
		*out = (grid_entry_t){"file_explorer", &icon_menu_file_explorer, NULL, filespage_open};
		return true;
	}
	if (item > HOME_CONTINUE_BOOK)
		return streaming_home_entry(ids[item], out);
	*out = (grid_entry_t){tags[item], icons[item], NULL, NULL, NULL};
	switch (item) {
	case HOME_MUSIC:
		out->target = &music_screen;
		out->subtitle = music_description;
		break;
	case HOME_STREAMING:
		out->target = &streaming_screen;
		out->subtitle = streaming_description;
		break;
	case HOME_NETWORKS:
		out->target = &wireless_screen;
		out->subtitle = network_description;
		break;
	case HOME_AUDIOBOOKS:
		out->target = &audiobooks_screen;
		out->subtitle = books_description;
		break;
	case HOME_SETTINGS:
		out->target = &settings_screen;
		break;
	case HOME_CONTINUE_BOOK:
		out->action = bookresume_play;
		out->subtitle = bookresume_description;
		break;
	default:
		return false;
	}
	return true;
}
static void rebuild(void) {
	load();
	if (tile_grid) {
		grid_entry_t entries[6] = {0};
		for (int i = 0; i < order_n; i++)
			main_menu_get_entry(order[i], &entries[i]);
		entries[5] = (grid_entry_t){"more", &icon_menu_more, &morepage_screen};
		gridpage_set_entries(tile_grid, page_cfg, entries, 6, 2, 3, false);
		for (int i = 0; i < order_n; i++)
			lv_obj_add_event_cb(lv_obj_get_child(tile_grid, i), main_menu_hold, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)order[i]);
		lv_obj_add_event_cb(lv_obj_get_child(tile_grid, 5), main_menu_hold, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)HOME_ITEM_COUNT);
	}
	morepage_rebuild();
}
static void save(void) {
	char value[128] = "";
	size_t used = 0;
	for (int i = 0; i < order_n; i++) {
		int n = snprintf(value + used, sizeof(value) - used, "%s%s", i ? "," : "", ids[order[i]]);
		if (n < 0 || (size_t)n >= sizeof(value) - used)
			return;
		used += (size_t)n;
	}
	config_set("home", "tiles", value);
	config_save();
	rebuild();
}
bool main_menu_move(home_item_t item, int pos) {
	load();
	if (!main_menu_available(item))
		return false;
	int old = -1;
	for (int i = 0; i < order_n; i++)
		if (order[i] == item)
			old = i;
	if (old < 0 && order_n == HOME_CUSTOM_SLOTS)
		return false;
	if (old >= 0) {
		memmove(order + old, order + old + 1, (size_t)(order_n - old - 1) * sizeof(*order));
		order_n--;
	}
	if (pos < 0)
		pos = 0;
	if (pos > order_n)
		pos = order_n;
	memmove(order + pos + 1, order + pos, (size_t)(order_n - pos) * sizeof(*order));
	order[pos] = item;
	order_n++;
	save();
	return true;
}
void main_menu_hide(home_item_t item) {
	load();
	for (int i = 0; i < order_n; i++)
		if (order[i] == item) {
			memmove(order + i, order + i + 1, (size_t)(order_n - i - 1) * sizeof(*order));
			order_n--;
			save();
			return;
		}
}
void main_menu_reset(void) {
	loaded = true;
	defaults();
	save();
}
void main_menu_reload(void) {
	loaded = false;
	rebuild();
}
void main_menu_refresh(void) {
	if (tile_grid)
		lv_obj_send_event(main_menu_screen, LV_EVENT_REFRESH, NULL);
	if (morepage_screen)
		lv_obj_send_event(morepage_screen, LV_EVENT_REFRESH, NULL);
}
void main_menu_open_settings(void) {
	gui_init_rest();
	hometiles_open(page_cfg);
}
static void settings_action(void *unused) {
	(void)unused;
	main_menu_open_settings();
}
static void move_action(void *user) {
	home_item_t item = (home_item_t)(intptr_t)user;
	if (main_menu_contains(item))
		main_menu_hide(item);
	else if (!main_menu_move(item, main_menu_count()))
		toast_error("home_full");
}
void main_menu_hold(lv_event_t *e) {
	if (player_sheet_drag_active() || switcher_back_drag_active() || coverflow_drag_active())
		return;
	lv_indev_t *input = lv_event_get_param(e);
	if (!input)
		input = lv_indev_active();
	if (input)
		lv_indev_wait_release(input);
	home_item_t item = (home_item_t)(intptr_t)lv_event_get_user_data(e);
	if (item == HOME_CONTINUE_BOOK) {
		bookresume_menu();
		return;
	}
	sx_sheet_item_t items[2];
	int n = 0;
	if (valid(item))
		items[n++] = (sx_sheet_item_t){.label = tr(main_menu_contains(item) ? (item <= HOME_CONTINUE_BOOK ? "home_to_more" : "hometiles_remove") : "home_to_main"), .icon = &icon_menu_more, .action = move_action, .user = (void *)(intptr_t)item};
	items[n++] = (sx_sheet_item_t){.label = tr("home_settings"), .icon = &icon_menu_settings, .action = settings_action};
	sx_sheet_menu(tr(main_menu_tag(item)), NULL, items, n);
}
void main_menu_init(gui_config_t *cfg) {
	page_cfg = cfg;
	tile_grid = gridpage_build(main_menu_screen, cfg, NULL, 0, 2, 3, false);
	rebuild();
	shufflecard_attach(main_menu_screen, cfg);
}

static void search_scope_action(void *user) {
	home_item_t source = (home_item_t)(intptr_t)user;
	if (source == HOME_YANDEX)
		yandexpage_search_open();
#ifdef SONIX_ZVUK
	else if (source == HOME_ZVUK)
		zvukpage_search_open();
#endif
	else
		search_open();
}
void main_menu_open_search(void) {
	gui_init_rest();
	sx_sheet_item_t items[3] = {{.label = tr("home_search_card"), .icon = &icon_menu_music, .action = search_scope_action, .user = (void *)(intptr_t)HOME_MUSIC}};
	int n = 1;
	if (main_menu_available(HOME_YANDEX))
		items[n++] = (sx_sheet_item_t){.label = tr("yandex"), .icon = main_menu_icon(HOME_YANDEX), .action = search_scope_action, .user = (void *)(intptr_t)HOME_YANDEX};
#ifdef SONIX_ZVUK
	if (main_menu_available(HOME_ZVUK))
		items[n++] = (sx_sheet_item_t){.label = tr("zvuk"), .icon = main_menu_icon(HOME_ZVUK), .action = search_scope_action, .user = (void *)(intptr_t)HOME_ZVUK};
#endif
	sx_sheet_menu(tr("home_search"), NULL, items, n);
}
