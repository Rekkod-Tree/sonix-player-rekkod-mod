/*
 * Storage ownership regression, using production USB/storage code with OS fixtures.
 * From sonix-player (LVGL headers must be available in the checkout/include path):
 * cc -std=gnu11 -O1 -g -DHOST_BUILD -DLV_CONF_INCLUDE_SIMPLE -I. \
 *   -ffunction-sections -fdata-sections tests/storage_handoff_test.c \
 *   -Wl,--gc-sections -pthread -o /tmp/storage_handoff_test
 * /tmp/storage_handoff_test
 * No actual card, mount, USB gadget, network or device changes are performed.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/statvfs.h>
#include <unistd.h>
static FILE *fixture_fopen(const char *, const char *);
static char *fixture_realpath(const char *, char *);
static int fixture_system(const char *);
static int fixture_statvfs(const char *, struct statvfs *);
static DIR *fixture_opendir(const char *);
static int fixture_open(const char *, int, ...);
static int fixture_close(int);
static int fixture_syncfs(int);
static int fixture_unlink(const char *);
static int fixture_access(const char *, int);
static unsigned fixture_sleep(unsigned);
static int fixture_mount(const char *, const char *, const char *, unsigned long, const void *);
static int fixture_umount(const char *);
static int fixture_umount2(const char *, int);
#define fopen fixture_fopen
#define realpath fixture_realpath
#define system fixture_system
#define statvfs(path, st) fixture_statvfs(path, st)
#define opendir fixture_opendir
#define open fixture_open
#define close fixture_close
#define syncfs fixture_syncfs
#define unlink fixture_unlink
#define access fixture_access
#define sleep fixture_sleep
#define mount fixture_mount
#define umount fixture_umount
#define umount2 fixture_umount2
#include "src/system/device/system.c"
#include "src/system/device/usb.c"
#undef fopen
#undef realpath
#undef system
#undef statvfs
#undef opendir
#undef open
#undef close
#undef syncfs
#undef unlink
#undef access
#undef sleep
#undef mount
#undef umount
#undef umount2

static bool mounted, busy, fail_mount, fail_mounts_read, fail_realpath, read_only, fail_stat;
static bool adb_running, fail_insert, fail_release, fail_medium_read;
static bool medium, elsewhere, partition;
static bool update_fixture, fail_sync, fail_pin_close, fail_pin_open;
static int exported_writes, mounted_writes, opens, closes, probes;
static int sync_calls, pin_closes, unmount_calls;
static int pin_fd = -1;
static int external_pin_fd = -1;
static storage_config_t config = {.device = "/dev/fixture", .mount_point = "/fixture/card", .manage_mount = true};
static system_runtime_t runtime = {.storage_cfg = &config};
static char *fixture_realpath(const char *p, char *out) {
	if (fail_realpath) {
		errno = EIO;
		return NULL;
	}
	if (strcmp(p, "/sys/class/block/mmcblk0") == 0) {
		strcpy(out, "/sys/devices/mmc/block/mmcblk0");
	} else if (strcmp(p, "/sys/class/block/mmcblk0p1") == 0) {
		strcpy(out, "/sys/devices/mmc/block/mmcblk0/mmcblk0p1");
	} else {
		strcpy(out, p);
	}
	return out;
}
static FILE *text_file(const char *text) {
	FILE *f = tmpfile();
	assert(f);
	fputs(text, f);
	rewind(f);
	return f;
}
static ssize_t medium_write_fixture(void *cookie, const char *buf, size_t n) {
	(void)cookie;
	(void)buf;
	bool insert = n > 1;
	if ((insert && fail_insert) || (!insert && fail_release)) {
		errno = EIO;
		return -1;
	}
	if (insert) {
		assert(!mounted && !elsewhere);
		exported_writes++;
	}
	medium = insert;
	return (ssize_t)n;
}
static FILE *fixture_fopen(const char *path, const char *mode) {
	if (strcmp(path, "/proc/mounts") == 0) {
		if (fail_mounts_read) {
			errno = EACCES;
			return NULL;
		}
		if (partition) {
			return text_file("/dev/mmcblk0p1 /elsewhere vfat rw 0 0\n");
		}
		return text_file(mounted ? "/dev/fixture /fixture/card vfat rw 0 0\n" : elsewhere ? "/dev/fixture /elsewhere vfat rw 0 0\n" : "rootfs / ubifs rw 0 0\n");
	}
	if (strstr(path, "/lun.0/file")) {
		if (mode[0] == 'r') {
			if (fail_medium_read) {
				errno = EIO;
				return NULL;
			}
			return text_file(medium ? "/dev/fixture\n" : "\n");
		}
		cookie_io_functions_t io = {.write = medium_write_fixture};
		return fopencookie(NULL, "w", io);
	}
	assert(!"unexpected fopen path");
	return NULL;
}
static int fixture_system(const char *cmd) {
	assert(!strstr(cmd, "umount -f") && !strstr(cmd, "umount -l"));
	if (strncmp(cmd, "umount ", 7) == 0) {
		if (!busy)
			mounted = false;
		return busy ? 1 : 0;
	}
	if (strncmp(cmd, "mount ", 6) == 0) {
		assert(!medium);
		mounted_writes++;
		if (!fail_mount)
			mounted = true;
		return fail_mount ? 1 : 0;
	}
	if (strncmp(cmd, "for u in ", 9) == 0)
		return 0;
	assert(!"unexpected shell command");
	return 1;
}
static int fixture_statvfs(const char *path, struct statvfs *st) {
	(void)path;
	if (fail_stat) {
		errno = EIO;
		return -1;
	}
	memset(st, 0, sizeof(*st));
	st->f_flag = read_only ? ST_RDONLY : 0;
	return 0;
}
static DIR *fixture_opendir(const char *path) {
	if (strcmp(path, "/fixture/card") == 0 && mounted)
		return opendir("/tmp");
	errno = ENOENT;
	return NULL;
}
static int fixture_open(const char *path, int flags, ...) {
	if (strcmp(path, "/fixture/card") == 0) {
		assert(mounted && (flags & O_DIRECTORY));
		assert(pin_fd < 0);
		if (fail_pin_open) {
			errno = EIO;
			return -1;
		}
		pin_fd = open("/tmp", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		return pin_fd;
	}
	(void)flags;
	assert(strstr(path, ".sonix-write-test"));
	assert(mounted);
	probes++;
	return open("/dev/null", O_WRONLY);
}
static int fixture_close(int fd) {
	bool pin = fd == pin_fd;
	int rc = close(fd);
	if (pin) {
		assert(rc == 0);
		pin_fd = -1;
		pin_closes++;
		if (fail_pin_close) {
			// Linux releases the descriptor even when close reports delayed I/O.
			errno = EIO;
			return -1;
		}
	}
	return rc;
}
static int fixture_syncfs(int fd) {
	assert(fd == pin_fd && fd >= 0 && fcntl(fd, F_GETFD) >= 0);
	assert(mounted);
	sync_calls++;
	if (fail_sync) {
		errno = EIO;
		return -1;
	}
	return 0;
}
static int fixture_unlink(const char *path) {
	assert(strstr(path, ".sonix-write-test"));
	return 0;
}
static int fixture_access(const char *path, int mode) {
	(void)mode;
	if (strcmp(path, "/dev/fixture") == 0)
		return 0;
	errno = ENOENT;
	return -1;
}
static unsigned fixture_sleep(unsigned n) {
	(void)n;
	return 0;
}
static int fixture_mount(const char *a, const char *b, const char *c, unsigned long flags, const void *d) {
	(void)a;
	(void)b;
	(void)c;
	if (update_fixture)
		assert(!(flags & (MS_REMOUNT | MS_RDONLY)));
	(void)d;
	assert(!medium);
	mounted_writes++;
	if (fail_mount) {
		errno = EIO;
		return -1;
	}
	mounted = true;
	return 0;
}
static int fixture_umount(const char *path) {
	(void)path;
	unmount_calls++;
	if (update_fixture) {
		// Even a read-only root descriptor pins a mount. syncfs must complete
		// and close it before the ordinary unmount is attempted.
		assert(pin_fd < 0 && sync_calls == 1 && pin_closes == 1);
	}
	if (busy || pin_fd >= 0 || external_pin_fd >= 0) {
		errno = EBUSY;
		return -1;
	}
	mounted = false;
	return 0;
}
static int fixture_umount2(const char *path, int flags) {
	(void)path;
	(void)flags;
	assert(!"forced unmount must not run in tested handoffs");
	return -1;
}
bool adb_is_running(void) { return adb_running; }
char *read_file_content(const char *path) {
	(void)path;
	return NULL;
}
bool sysserver_available(void) { return false; }
int sysserver_umount(const char *path) { return fixture_umount(path); }
int sysserver_mount(const char *a, const char *b) { return fixture_mount(a, b, NULL, 0, NULL); }
bool gui_post(void (*cb)(void *), void *user) {
	(void)cb;
	(void)user;
	return true;
}
void gui_notify_popup(const char *s) { (void)s; }
void browser_refresh(void) {}
void device_state_note_storage_gone(void) {}
void audio_stop(void) {}
void library_close(void) { closes++; }
void audiobookdb_close(void) { closes++; }
void radio_store_close(void) { closes++; }
void logging_suspend_for_usb(void) {}
void logging_child_stdio(void) {}
void btlog_card_release(void) {}
void streamturn_abandon_all(void) {}
static void root_checked(const char *root) {
	if (root) {
		assert(mounted);
		assert(!storage_active);
		assert(!card_released && !card_usb_claimed && !card_update_claimed);
	}
}
bool library_open(const char *path) {
	root_checked(path);
	opens++;
	return true;
}
bool audiobookdb_open(const char *path) {
	root_checked(path);
	opens++;
	return true;
}
bool radio_store_open(const char *path) {
	root_checked(path);
	opens++;
	return true;
}
bool library_card_returned(const char *root) {
	root_checked(root);
	return true;
}
#define ROOT_STUB(name)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                \
	void name(const char *root) { root_checked(root); }
ROOT_STUB(playlists_init)
ROOT_STUB(screensaver_set_card_root)
ROOT_STUB(cover_set_cache_dir)
ROOT_STUB(eq_presets_set_dir)
ROOT_STUB(peq_presets_set_dir)
ROOT_STUB(mseb_presets_set_dir)
ROOT_STUB(stream_provider_set_root_all)
ROOT_STUB(cardimport_set_root)
ROOT_STUB(podcastcache_set_root)
ROOT_STUB(podcastsubs_set_root)
ROOT_STUB(podcaststate_set_root)
ROOT_STUB(dlna_set_root)
ROOT_STUB(gbdb_set_root)
ROOT_STUB(logging_attach_sd)
ROOT_STUB(btlog_card_attach)

static void reset(void) {
	assert(pin_fd < 0);
	assert(external_pin_fd < 0);
	mounted = true;
	busy = fail_mount = fail_mounts_read = fail_realpath = read_only = fail_stat = false;
	adb_running = fail_insert = fail_release = fail_medium_read = medium = elsewhere = partition = false;
	update_fixture = fail_sync = fail_pin_close = fail_pin_open = false;
	exported_writes = mounted_writes = opens = closes = probes = 0;
	sync_calls = pin_closes = unmount_calls = 0;
	storage_active = false;
	export_mode = EXPORT_NONE;
	dac_owns_usb = false;
	suppress_popups = true;
	card_usb_claimed = card_released = card_update_claimed = false;
	card_attached = true;
	sd_writable = true;
	sd_requires_mount = true;
	strcpy(sd_mount, "/fixture/card");
	strcpy(sd_root, sd_mount);
	strcpy(sd_device, "/dev/fixture");
	g_storage_cfg = &config;
	g_runtime = &runtime;
}
static void *export_thread(void *unused) {
	(void)unused;
	storage_export();
	return NULL;
}
static void update_handoffs(void) {
	char root[PATH_MAX];
	reset();
	update_fixture = true;
	assert(storage_prepare_for_update());
	assert(!mounted && !card_attached && card_released && card_update_claimed && !card_usb_claimed);
	assert(sync_calls == 1 && pin_closes == 1 && unmount_calls == 1 && opens == 0);
	assert(storage_pin_card(root, sizeof(root)) < 0 && !root[0]);
	assert(!storage_release_for_usb() && !card_usb_claimed);
	storage_recheck_card();
	assert(!mounted && mounted_writes == 0);
	assert(!storage_prepare_for_update());
	assert(sync_calls == 1 && unmount_calls == 1);
	storage_cancel_update();
	assert(mounted && card_attached && opens == 3 && mounted_writes == 1);
	assert(!card_released && !card_usb_claimed && storage_sd_writable());
	storage_cancel_update();
	assert(opens == 3 && mounted_writes == 1);

	// The installer must not reboot after delayed I/O reported by either
	// syncfs or close, nor after EBUSY. Live local services must come back.
	for (int failure = 0; failure < 4; failure++) {
		reset();
		update_fixture = true;
		fail_sync = failure == 0;
		fail_pin_close = failure == 1;
		busy = failure == 2;
		fail_pin_open = failure == 3;
		assert(!storage_prepare_for_update());
		assert(errno == (failure == 2 ? EBUSY : EIO));
		assert(mounted && card_attached && !card_released && !card_usb_claimed);
		assert(pin_fd < 0 && mounted_writes == 0 && exported_writes == 0);
		assert(unmount_calls == (failure == 2 ? 1 : 0));
		if (failure != 3) {
			assert(sync_calls == 1 && pin_closes == 1 && opens == 3);
		} else {
			assert(sync_calls == 0 && pin_closes == 0);
		}
		fail_sync = fail_pin_close = busy = fail_pin_open = false;
		int fd = storage_pin_card(root, sizeof(root));
		assert(fd >= 0 && strcmp(root, "/fixture/card") == 0);
		assert(fixture_close(fd) == 0);
	}

	reset();
	update_fixture = true;
	int writer_pin = storage_pin_card(root, sizeof(root));
	assert(writer_pin >= 0);
	external_pin_fd = writer_pin;
	pin_fd = -1; // The update now acquires its own independent root descriptor.
	assert(!storage_prepare_for_update() && errno == EBUSY);
	assert(mounted && card_attached && opens == 3 && !card_released && !card_update_claimed);
	assert(fcntl(writer_pin, F_GETFD) >= 0); // Rollback must not close another writer's pin.
	assert(close(writer_pin) == 0);
	external_pin_fd = -1;

	reset();
	update_fixture = true;
	mounted = card_attached = false;
	assert(!storage_prepare_for_update());
	storage_cancel_update();
	assert(!mounted && !card_attached && mounted_writes == 0);
	assert(sync_calls == 0 && pin_closes == 0 && unmount_calls == 0 && closes == 0);

	reset();
	update_fixture = true;
	storage_active = medium = card_usb_claimed = true;
	mounted = card_attached = false;
	assert(!storage_prepare_for_update());
	storage_cancel_update();
	assert(storage_active && medium && card_usb_claimed && !mounted && !card_attached);
	assert(sync_calls == 0 && pin_closes == 0 && unmount_calls == 0 && mounted_writes == 0);

	reset();
	update_fixture = true;
	card_usb_claimed = true; // USB owns admission before its LUN is populated.
	assert(!storage_prepare_for_update());
	storage_cancel_update();
	assert(card_usb_claimed && mounted && card_attached && closes == 0);
	assert(sync_calls == 0 && unmount_calls == 0);

	reset();
	update_fixture = true;
	card_released = true; // Shutdown's permanent claim belongs to another path.
	assert(!storage_prepare_for_update());
	storage_cancel_update();
	assert(card_released && mounted && card_attached && closes == 0);
	assert(sync_calls == 0 && unmount_calls == 0);

	reset();
	update_fixture = true;
	assert(storage_prepare_for_update());
	fail_mount = true;
	storage_cancel_update();
	assert(!mounted && !card_attached && !card_released && !card_usb_claimed && opens == 0);
	fail_mount = false;
	storage_recheck_card();
	assert(mounted && card_attached && opens == 3);
	puts("PASS: update sync/close/unmount ordering, failures, cancellation and ownership");
}
int main(void) {
	reset();
	char pinned_root[PATH_MAX];
	int held = storage_pin_card(pinned_root, sizeof(pinned_root));
	assert(held >= 0 && strcmp(pinned_root, sd_root) == 0);
	storage_export();
	assert(mounted && !medium && !storage_active && card_attached);
	assert(fcntl(held, F_GETFD) >= 0);
	assert(close(held) == 0);
	pin_fd = -1;
	storage_export();
	assert(medium && !mounted);
	assert(storage_pin_card(pinned_root, sizeof(pinned_root)) < 0 && !pinned_root[0]);
	storage_restore();
	assert(mounted && !medium);

	reset();
	pthread_mutex_lock(&card_lock);
	pthread_t th;
	assert(pthread_create(&th, NULL, export_thread, NULL) == 0);
	while (!storage_active)
		sched_yield();
	assert(!medium && mounted);
	pthread_mutex_unlock(&card_lock);
	assert(pthread_join(th, NULL) == 0);
	assert(medium && !mounted && card_usb_claimed);
	storage_restore();
	assert(mounted && !medium && card_attached);
	reset();
	storage_export();
	assert(medium && !mounted && storage_active && card_usb_claimed && !card_attached);
	assert(exported_writes == 1 && closes == 3);
	storage_recheck_card();
	assert(!mounted && mounted_writes == 0);
	storage_restore();
	assert(!medium && mounted && !storage_active && !card_usb_claimed && card_attached && opens == 3);
	reset();
	busy = true;
	storage_export();
	assert(mounted && !medium && !storage_active && !card_usb_claimed && card_attached && opens == 3 && exported_writes == 0);
	reset();
	fail_mounts_read = true;
	storage_export();
	assert(!medium && !storage_active && exported_writes == 0 && opens == 0);
	reset();
	fail_realpath = true;
	storage_export();
	assert(!medium && !storage_active && exported_writes == 0 && opens == 0);
	reset();
	elsewhere = true;
	mounted = false;
	storage_export();
	assert(!medium && exported_writes == 0);
	reset();
	mounted = false;
	partition = true;
	strcpy(sd_device, "/dev/mmcblk0");
	storage_export();
	assert(!medium && exported_writes == 0 && opens == 0);
	reset();
	adb_running = true;
	storage_export();
	assert(mounted && !storage_active && closes == 0 && exported_writes == 0);
	reset();
	fail_insert = true;
	storage_export();
	assert(!medium && mounted && !storage_active && opens == 3);
	reset();
	storage_export();
	fail_release = true;
	storage_restore();
	assert(medium && !mounted && storage_active && card_usb_claimed && opens == 0 && mounted_writes == 0);
	fail_release = false;
	storage_restore();
	assert(!medium && mounted && !storage_active && opens == 3);
	reset();
	storage_export();
	fail_medium_read = true;
	storage_restore();
	assert(!mounted && storage_active && card_usb_claimed && opens == 0);
	reset();
	storage_export();
	fail_mount = true;
	storage_restore();
	assert(!medium && !mounted && !storage_active && !card_attached && opens == 0);
	assert(!storage_sd_root() && !storage_sd_writable());
	reset();
	read_only = true;
	assert(!storage_sd_writable());
	storage_recheck_card();
	assert(!sd_writable && probes == 0);
	reset();
	fail_stat = true;
	assert(!storage_sd_writable());
	reset();
	mounted = false;
	assert(!storage_sd_root() && !storage_sd_writable());
	assert(strcmp(storage_sd_mount_point(), "/fixture/card") == 0);
	puts("PASS: USB unmount/release failures, ADB, mount ownership, absent/read-only storage");
	update_handoffs();
	return 0;
}
