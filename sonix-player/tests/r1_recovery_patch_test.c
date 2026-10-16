// Offline OS fixtures: no MTD device is opened and no ioctl reaches the kernel.
// cc -std=gnu11 -O2 -D_FILE_OFFSET_BITS=64 -I. tests/r1_recovery_patch_test.c src/system/core/sha256.c -o /tmp/r1_recovery_patch_test
// /tmp/r1_recovery_patch_test original-24MiB-dump.bin reviewed-128KiB-block.bin
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <mtd/mtd-user.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

static int fixture_open(const char *, int, ...);
static int fixture_close(int);
static int fixture_fstat(int, struct stat *);
static ssize_t fixture_pread(int, void *, size_t, off_t);
static ssize_t fixture_pwrite(int, const void *, size_t, off_t);
static int fixture_ioctl(int, unsigned long, ...);
static int fixture_flock(int, int);
#define open fixture_open
#define close fixture_close
#define fstat fixture_fstat
#define pread fixture_pread
#define pwrite fixture_pwrite
#define ioctl fixture_ioctl
#define flock fixture_flock
#define main recovery_patch_main
#include "tools/r1_recovery_patch.c"
#undef open
#undef close
#undef fstat
#undef pread
#undef pwrite
#undef ioctl
#undef flock
#undef main

enum scenario { CHECK_OLD, CHECK_NEW, APPLY_OLD, APPLY_NEW, WRONG_MODEL, WRONG_SELECTOR, WRONG_GEOMETRY, BAD_BLOCK, BAD_BLOCK_QUERY, WRONG_WHOLE_HASH, WRONG_NEW_HASH, WRONG_NEW_SIZE, WRONG_OLD_BLOCK, FAILED_ERASE, FAILED_WRITE, SHORT_WRITE, FAILED_WRITE_CLOSE, WRONG_READBACK, FAILED_WRITE_OPEN, READ_EINTR, SHORT_READ, WRONG_SELECTOR_PADDING, LOCK_BUSY, RECREATED_NODE };
enum kind { MODEL_FD, SELECTOR_FD, RECOVERY_FD, BLOCK_FD };
static enum scenario scenario;
static unsigned char *original, *working;
static unsigned char replacement[R1_RECOVERY_ERASE_SIZE];
static int erased, write_calls, writable_opens, readback_opens;
static bool interrupted;
static int locked_slot = -1, lock_calls;
static struct {
	bool open, complete_read;
	enum kind kind;
	int flags;
	unsigned badblock_queries;
} descriptors[16];

static int fixture_open(const char *path, int flags, ...) {
	enum kind kind;
	if (!strcmp(path, R1_INFO))
		kind = MODEL_FD;
	else if (!strcmp(path, R1_SELECTOR))
		kind = SELECTOR_FD;
	else if (!strcmp(path, R1_RECOVERY_DEVICE))
		kind = RECOVERY_FD;
	else if (!strcmp(path, "/fixture/new-block.bin"))
		kind = BLOCK_FD;
	else {
		assert(!"unexpected OS path");
		return -1;
	}
	assert((flags & O_CLOEXEC) && (flags & O_NOFOLLOW));
	assert((flags & O_ACCMODE) == O_RDONLY || (kind == RECOVERY_FD && (flags & O_ACCMODE) == O_RDWR));
	if ((flags & O_ACCMODE) == O_RDWR) {
		assert(locked_slot >= 0);
		writable_opens++;
		if (scenario == FAILED_WRITE_OPEN) {
			errno = EACCES;
			return -1;
		}
	}
	if (kind == RECOVERY_FD && (flags & O_ACCMODE) == O_RDONLY && erased) {
		assert(locked_slot >= 0);
		readback_opens++;
	}
	for (int i = 0; i < 16; i++) {
		if (!descriptors[i].open) {
			descriptors[i].open = true;
			descriptors[i].complete_read = false;
			descriptors[i].kind = kind;
			descriptors[i].flags = flags;
			descriptors[i].badblock_queries = 0;
			return 100 + i;
		}
	}
	assert(!"descriptor leak");
	return -1;
}

static int slot(int fd) {
	assert(fd >= 100 && fd < 116 && descriptors[fd - 100].open);
	return fd - 100;
}

static int fixture_close(int fd) {
	int i = slot(fd);
	descriptors[i].open = false;
	if (locked_slot == i)
		locked_slot = -1;
	if (scenario == FAILED_WRITE_CLOSE && descriptors[i].kind == RECOVERY_FD && (descriptors[i].flags & O_ACCMODE) == O_RDWR && erased) {
		errno = EIO;
		return -1;
	}
	return 0;
}

static int fixture_flock(int fd, int operation) {
	int i = slot(fd);
	assert(descriptors[i].kind == RECOVERY_FD && (descriptors[i].flags & O_ACCMODE) == O_RDONLY);
	assert(operation == (LOCK_EX | LOCK_NB) && locked_slot < 0);
	lock_calls++;
	if (scenario == LOCK_BUSY) {
		errno = EWOULDBLOCK;
		return -1;
	}
	locked_slot = i;
	return 0;
}

static const char *model_json(void) { return scenario == WRONG_MODEL ? "{\"device-name\":\"HiBy R3 Pro II\"}" : "{\"OS_version\":\"test\",\"device-name\":\"HiBy R1\"}"; }

static int fixture_fstat(int fd, struct stat *st) {
	int i = slot(fd);
	memset(st, 0, sizeof(*st));
	st->st_mode = descriptors[i].kind == MODEL_FD || descriptors[i].kind == BLOCK_FD ? S_IFREG | 0600 : S_IFCHR | 0600;
	if (descriptors[i].kind == MODEL_FD)
		st->st_size = (off_t)strlen(model_json());
	if (descriptors[i].kind == BLOCK_FD)
		st->st_size = R1_RECOVERY_ERASE_SIZE - (scenario == WRONG_NEW_SIZE);
	if (descriptors[i].kind == RECOVERY_FD) {
		st->st_dev = 7;
		st->st_ino = scenario == RECREATED_NODE && (descriptors[i].flags & O_ACCMODE) == O_RDWR ? 5 : 4;
		st->st_rdev = 123;
	}
	return 0;
}

static ssize_t fixture_pread(int fd, void *out, size_t size, off_t offset) {
	int i = slot(fd);
	assert(offset >= 0);
	if (scenario == READ_EINTR && !interrupted) {
		interrupted = true;
		errno = EINTR;
		return -1;
	}
	if (scenario == SHORT_READ && descriptors[i].kind == RECOVERY_FD)
		return 0;
	unsigned char selector[SELECTOR_SIZE];
	memset(selector, ' ', sizeof(selector));
	memcpy(selector, scenario == WRONG_SELECTOR ? "ota:kernel2" : "ota:kernel", scenario == WRONG_SELECTOR ? 11 : 10);
	if (scenario == WRONG_SELECTOR_PADDING)
		selector[SELECTOR_SIZE - 1] = '\0';
	const void *source;
	size_t total;
	switch (descriptors[i].kind) {
	case MODEL_FD:
		source = model_json();
		total = strlen(model_json());
		break;
	case SELECTOR_FD:
		source = selector;
		total = sizeof(selector);
		break;
	case RECOVERY_FD:
		source = working;
		total = R1_RECOVERY_SIZE;
		break;
	case BLOCK_FD:
		source = replacement;
		total = sizeof(replacement);
		break;
	default:
		abort();
	}
	assert((size_t)offset <= total && size <= total - (size_t)offset);
	memcpy(out, (const unsigned char *)source + offset, size);
	if (descriptors[i].kind == BLOCK_FD && scenario == WRONG_NEW_HASH && offset == 0)
		((unsigned char *)out)[0] ^= 1;
	if (descriptors[i].kind == RECOVERY_FD) {
		if (scenario == WRONG_OLD_BLOCK && descriptors[i].complete_read && offset == R1_RECOVERY_BLOCK_OFFSET)
			((unsigned char *)out)[0] ^= 1;
		if (scenario == WRONG_READBACK && erased && (descriptors[i].flags & O_ACCMODE) == O_RDONLY && offset == 0)
			((unsigned char *)out)[0] ^= 1;
		if ((size_t)offset + size == R1_RECOVERY_SIZE)
			descriptors[i].complete_read = true;
	}
	return (ssize_t)size;
}

static int fixture_ioctl(int fd, unsigned long request, ...) {
	int i = slot(fd);
	assert(descriptors[i].kind == RECOVERY_FD);
	va_list ap;
	va_start(ap, request);
	void *arg = va_arg(ap, void *);
	va_end(ap);
	if (request == MEMGETINFO) {
		struct mtd_info_user *info = arg;
		memset(info, 0, sizeof(*info));
		info->type = MTD_NANDFLASH;
		info->size = R1_RECOVERY_SIZE;
		info->erasesize = R1_RECOVERY_ERASE_SIZE;
		info->writesize = R1_RECOVERY_WRITE_SIZE + (scenario == WRONG_GEOMETRY);
		return 0;
	}
	if (request == MEMGETBADBLOCK) {
		int64_t offset = *(int64_t *)arg;
		assert(offset >= 0 && offset < R1_RECOVERY_SIZE && offset % R1_RECOVERY_ERASE_SIZE == 0);
		assert(offset == (int64_t)descriptors[i].badblock_queries * R1_RECOVERY_ERASE_SIZE);
		descriptors[i].badblock_queries++;
		// Even a bad block far from the replacement invalidates raw/BBT mapping.
		if (scenario == BAD_BLOCK && offset == R1_RECOVERY_SIZE - R1_RECOVERY_ERASE_SIZE)
			return 1;
		if (scenario == BAD_BLOCK_QUERY && offset == R1_RECOVERY_BLOCK_OFFSET) {
			errno = EIO;
			return -1;
		}
		return 0;
	}
	assert(request == MEMERASE);
	assert(locked_slot >= 0);
	assert((descriptors[i].flags & O_ACCMODE) == O_RDWR && erased == 0);
	assert(descriptors[i].complete_read && descriptors[i].badblock_queries == R1_RECOVERY_SIZE / R1_RECOVERY_ERASE_SIZE);
	struct erase_info_user *erase = arg;
	assert(erase->start == R1_RECOVERY_BLOCK_OFFSET && erase->length == R1_RECOVERY_ERASE_SIZE);
	erased++;
	if (scenario == FAILED_ERASE) {
		errno = EIO;
		return -1;
	}
	memset(working + erase->start, 0xff, erase->length);
	return 0;
}

static ssize_t fixture_pwrite(int fd, const void *data, size_t size, off_t offset) {
	int i = slot(fd);
	assert(locked_slot >= 0);
	assert(descriptors[i].kind == RECOVERY_FD && (descriptors[i].flags & O_ACCMODE) == O_RDWR && erased == 1);
	assert(size == R1_RECOVERY_WRITE_SIZE && offset == R1_RECOVERY_BLOCK_OFFSET + write_calls * R1_RECOVERY_WRITE_SIZE);
	assert(offset + (off_t)size <= R1_RECOVERY_BLOCK_OFFSET + R1_RECOVERY_ERASE_SIZE);
	write_calls++;
	if (write_calls == 5 && scenario == FAILED_WRITE) {
		errno = EIO;
		return -1;
	}
	if (write_calls == 5 && scenario == SHORT_WRITE)
		size /= 2;
	memcpy(working + offset, data, size);
	return (ssize_t)size;
}

static void load_fixture(const char *path, void *data, size_t size) {
	FILE *file = fopen(path, "rb");
	assert(file && fread(data, 1, size, file) == size && fgetc(file) == EOF && !ferror(file));
	assert(fclose(file) == 0);
}

static void expect_hash(const void *data, size_t size, const char *expected) {
	sha256_ctx context;
	char digest[SHA256_HEX_LEN];
	sha256_init(&context);
	sha256_update(&context, data, size);
	sha256_final_hex(&context, digest);
	assert(strcmp(digest, expected) == 0);
}

int main(int argc, char **argv) {
	assert(argc == 3);
	original = malloc(R1_RECOVERY_SIZE);
	working = malloc(R1_RECOVERY_SIZE);
	assert(original && working);
	load_fixture(argv[1], original, R1_RECOVERY_SIZE);
	load_fixture(argv[2], replacement, sizeof(replacement));
	expect_hash(original, R1_RECOVERY_SIZE, R1_RECOVERY_OLD_SHA256);
	expect_hash(replacement, sizeof(replacement), R1_RECOVERY_NEW_BLOCK_SHA256);
	for (scenario = CHECK_OLD; scenario <= RECREATED_NODE; scenario++) {
		memcpy(working, original, R1_RECOVERY_SIZE);
		memset(descriptors, 0, sizeof(descriptors));
		erased = write_calls = writable_opens = readback_opens = 0;
		lock_calls = 0;
		assert(locked_slot == -1);
		interrupted = false;
		bool is_new = scenario == CHECK_NEW || scenario == APPLY_NEW;
		if (is_new) {
			memcpy(working + R1_RECOVERY_BLOCK_OFFSET, replacement, sizeof(replacement));
			expect_hash(working, R1_RECOVERY_SIZE, R1_RECOVERY_NEW_SHA256);
		}
		if (scenario == WRONG_WHOLE_HASH)
			working[0] ^= 1;
		char *check_args[] = {"r1_recovery_patch", "--check", NULL};
		char *apply_args[] = {"r1_recovery_patch", "--apply", "/fixture/new-block.bin", NULL};
		bool check_only = scenario == CHECK_OLD || scenario == CHECK_NEW || scenario == READ_EINTR;
		int rc = recovery_patch_main(check_only ? 2 : 3, check_only ? check_args : apply_args);
		bool success = scenario == CHECK_OLD || scenario == CHECK_NEW || scenario == APPLY_OLD || scenario == APPLY_NEW || scenario == READ_EINTR;
		assert((rc == 0) == success);
		for (int i = 0; i < 16; i++)
			assert(!descriptors[i].open);
		assert(locked_slot == -1);
		if (check_only)
			assert(lock_calls == 0);
		bool reaches_erase = scenario == APPLY_OLD || (scenario >= FAILED_ERASE && scenario <= WRONG_READBACK);
		if (!reaches_erase) {
			assert(erased == 0 && write_calls == 0);
			if (scenario != FAILED_WRITE_OPEN && scenario != RECREATED_NODE)
				assert(writable_opens == 0);
		} else {
			assert(erased == 1 && writable_opens == 1);
			int wanted = scenario == FAILED_ERASE ? 0 : (scenario == FAILED_WRITE || scenario == SHORT_WRITE) ? 5 : 64;
			assert(write_calls == wanted);
			assert(readback_opens == (scenario == APPLY_OLD || scenario == WRONG_READBACK));
		}
		if (scenario == APPLY_OLD)
			expect_hash(working, R1_RECOVERY_SIZE, R1_RECOVERY_NEW_SHA256);
		printf("PASS offline recovery scenario %d\n", scenario);
	}
	free(working);
	free(original);
	return 0;
}
