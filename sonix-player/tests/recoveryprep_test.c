// Linux-only fixture: all flash paths are ordinary temporary files and the
// spawned helper is a fixture script. Never run the real repair helper here.
// cc -std=gnu11 -O1 -Wall -I. tests/recoveryprep_test.c src/system/core/sha256.c -o /tmp/recoveryprep_test
#define _GNU_SOURCE
#include "src/system/core/sha256.h"
#include "src/system/device/recovery_r1.h"
#include <assert.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

static char root[] = "/tmp/sonix-recovery-fixture-XXXXXX";
static char source[256], kernel[256], helper[256], state_file[256], calls[256];
static char original_sha[SHA256_HEX_LEN];
static bool admitted = true, external = true, fail_sync, fail_write, fail_close, no_space;
static char *percent = "90";
static time_t tick = 1900000000;
static time_t fixture_time(time_t *out) {
	time_t now = tick++;
	if (out)
		*out = now;
	return now;
}
static int fixture_fsync(int fd) { return fail_sync ? -1 : fsync(fd); }
static ssize_t fixture_write(int fd, const void *data, size_t bytes) { return fail_write ? 0 : write(fd, data, bytes); }
static int fixture_close(int fd) {
	struct stat st;
	bool regular = fstat(fd, &st) == 0 && S_ISREG(st.st_mode);
	int rc = close(fd);
	if (fail_close && regular) {
		fail_close = false;
		return -1;
	}
	return rc;
}
static int fixture_statvfs(int fd, struct statvfs *st) {
	int rc = fstatvfs(fd, st);
	if (no_space && !rc)
		st->f_bavail = 0;
	return rc;
}
#undef R1_RECOVERY_DEVICE
#undef R1_RECOVERY_OLD_SHA256
#define R1_RECOVERY_DEVICE source
#define R1_RECOVERY_OLD_SHA256 original_sha
#define RECOVERY_PREP_HELPER helper
#define RECOVERY_PREP_KERNEL kernel
#define time fixture_time
#define fsync fixture_fsync
#define write fixture_write
#define close fixture_close
#define fstatvfs fixture_statvfs
#include "src/system/device/recoveryprep.c"
#undef time
#undef fsync
#undef write
#undef close
#undef fstatvfs

void sync_battery_from_sysfs(void) {}
char *read_battery_percent(void) { return percent; }
bool usb_vbus_present(void) { return external; }
bool storage_sd_writable(void) { return admitted; }
int storage_pin_card(char *out, size_t size) {
	if (!admitted)
		return -1;
	snprintf(out, size, "%s", root);
	return open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
}
static void put(const char *path, const char *text) {
	FILE *f = fopen(path, "wb");
	assert(f);
	assert(fputs(text, f) >= 0);
	assert(fclose(f) == 0);
}
static void set_old(void) {
	put(state_file, "old\n");
	setenv("SONIX_RECOVERY_APPLY", "success", 1);
}
static void backup_ok(void) {
	set_old();
	assert(recovery_prep_backup() == RECOVERY_PREP_BACKED_UP);
}
static int remove_path(const char *path, const struct stat *st, int type, struct FTW *walk) {
	(void)st;
	(void)type;
	(void)walk;
	return remove(path);
}

int main(void) {
	assert(mkdtemp(root));
	snprintf(source, sizeof(source), "%s/source.bin", root);
	snprintf(kernel, sizeof(kernel), "%s/kernel.bin", root);
	snprintf(helper, sizeof(helper), "%s/helper", root);
	snprintf(state_file, sizeof(state_file), "%s/state", root);
	snprintf(calls, sizeof(calls), "%s/calls", root);
	setenv("SONIX_RECOVERY_STATE", state_file, 1);
	setenv("SONIX_RECOVERY_CALLS", calls, 1);
	put(helper, "#!/bin/sh\ncase \"$1\" in\n--check)\n read state < \"$SONIX_RECOVERY_STATE\"\n case \"$state\" in\n old) echo R1_RECOVERY_CHECK_READY:;;\n new) echo R1_RECOVERY_ALREADY_APPLIED:;;\n *) exit 1;;\n esac;;\n--apply)\n echo apply >> \"$SONIX_RECOVERY_CALLS\"\n case \"$SONIX_RECOVERY_APPLY\" in\n success) echo new > \"$SONIX_RECOVERY_STATE\";;\n error-new) echo new > \"$SONIX_RECOVERY_STATE\"; exit 1;;\n error-unknown) echo unknown > \"$SONIX_RECOVERY_STATE\"; exit 1;;\n *) exit 1;;\n esac;;\n*) exit 1;;\nesac\n");
	assert(chmod(helper, 0700) == 0);
	int fd = open(source, O_RDWR | O_CREAT | O_TRUNC, 0600);
	assert(fd >= 0);
	assert(ftruncate(fd, R1_RECOVERY_SIZE) == 0);
	close(fd);
	fd = open(kernel, O_RDWR | O_CREAT | O_TRUNC, 0600);
	assert(fd >= 0);
	assert(ftruncate(fd, 5u * 1024u * 1024u) == 0);
	close(fd);
	sha256_ctx hash;
	sha256_init(&hash);
	unsigned char zeros[16384] = {0};
	for (size_t n = 0; n < R1_RECOVERY_SIZE; n += sizeof(zeros))
		sha256_update(&hash, zeros, sizeof(zeros));
	sha256_final_hex(&hash, original_sha);
	set_old();
	assert(recovery_prep_check() == RECOVERY_PREP_READY);
	assert(recovery_prep_apply() == RECOVERY_PREP_FAILED);
	assert(access(calls, F_OK) != 0);
	admitted = false;
	assert(recovery_prep_backup() == RECOVERY_PREP_FAILED);
	admitted = true;
	no_space = true;
	assert(recovery_prep_backup() == RECOVERY_PREP_FAILED);
	no_space = false;
	fail_sync = true;
	assert(recovery_prep_backup() == RECOVERY_PREP_FAILED);
	fail_sync = false;
	fail_write = true;
	assert(recovery_prep_backup() == RECOVERY_PREP_FAILED);
	fail_write = false;
	fail_close = true;
	assert(recovery_prep_backup() == RECOVERY_PREP_FAILED);
	fail_close = false;
	assert(truncate(source, R1_RECOVERY_SIZE - 1) == 0);
	assert(recovery_prep_backup() == RECOVERY_PREP_FAILED);
	assert(truncate(source, R1_RECOVERY_SIZE) == 0);
	assert(access(calls, F_OK) != 0);
	backup_ok();
	char path[512];
	snprintf(path, sizeof(path), "%s/%s/rootfs2.bin", root, saved_directory);
	fd = open(path, O_WRONLY);
	assert(fd >= 0);
	assert(write(fd, "!", 1) == 1);
	close(fd);
	assert(recovery_prep_apply() == RECOVERY_PREP_FAILED);
	assert(access(calls, F_OK) != 0);
	backup_ok();
	snprintf(path, sizeof(path), "%s/%s/kernel2.bin", root, saved_directory);
	assert(unlink(path) == 0);
	assert(recovery_prep_apply() == RECOVERY_PREP_FAILED);
	assert(access(calls, F_OK) != 0);
	backup_ok();
	external = false;
	assert(recovery_prep_apply() == RECOVERY_PREP_FAILED);
	external = true;
	percent = "!!";
	assert(recovery_prep_apply() == RECOVERY_PREP_FAILED);
	percent = "49";
	assert(recovery_prep_apply() == RECOVERY_PREP_FAILED);
	percent = "90";
	assert(access(calls, F_OK) != 0);
	setenv("SONIX_RECOVERY_APPLY", "error-old", 1);
	assert(recovery_prep_apply() == RECOVERY_PREP_FAILED);
	backup_ok();
	setenv("SONIX_RECOVERY_APPLY", "error-new", 1);
	assert(recovery_prep_apply() == RECOVERY_PREP_FIXED);
	backup_ok();
	setenv("SONIX_RECOVERY_APPLY", "error-unknown", 1);
	assert(recovery_prep_apply() == RECOVERY_PREP_UNCERTAIN);
	backup_ok();
	assert(recovery_prep_apply() == RECOVERY_PREP_FIXED);
	assert(recovery_prep_check() == RECOVERY_PREP_FIXED);
	assert(recovery_prep_apply() == RECOVERY_PREP_FAILED);
	assert(nftw(root, remove_path, 16, FTW_DEPTH | FTW_PHYS) == 0);
	puts("PASS recovery preparation: backup IO failures, tampering, missing backup, power, helper old/new/unknown; no raw devices used");
}
