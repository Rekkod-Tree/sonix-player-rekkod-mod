// Normal log text, rotation failures and USB handoff, without player hardware.
// cc -std=gnu11 -Wall -Wextra -DHOST_BUILD -I. tests/logging_rotation_test.c -pthread -o /tmp/logging_rotation_test
// /tmp/logging_rotation_test
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static bool fail_temp, fail_read, fail_rename, fail_truncate;
static int test_mkostemp(char *path, int flags);
static ssize_t test_pread(int fd, void *data, size_t size, off_t offset);
static int test_rename(const char *from, const char *to);
static int test_ftruncate(int fd, off_t size);
#define mkostemp test_mkostemp
#define pread test_pread
#define rename test_rename
#define ftruncate test_ftruncate
#include "src/system/core/logging.c"
#undef mkostemp
#undef pread
#undef rename
#undef ftruncate

static int test_mkostemp(char *path, int flags) {
	if (fail_temp) {
		errno = EROFS;
		return -1;
	}
	return mkostemp(path, flags);
}
static ssize_t test_pread(int fd, void *data, size_t size, off_t offset) {
	if (fail_read) {
		errno = EIO;
		return -1;
	}
	return pread(fd, data, size, offset);
}
static int test_rename(const char *from, const char *to) {
	if (fail_rename) {
		errno = EROFS;
		return -1;
	}
	return rename(from, to);
}
static int test_ftruncate(int fd, off_t size) {
	if (fail_truncate) {
		errno = EIO;
		return -1;
	}
	return ftruncate(fd, size);
}

static bool enabled = true;
bool config_get_bool(const char *section, const char *key, bool fallback) {
	(void)section;
	(void)key;
	(void)fallback;
	return enabled;
}
void config_set_bool(const char *section, const char *key, bool value) {
	(void)section;
	(void)key;
	enabled = value;
}
bool config_save(void) { return true; }
const char *tr(const char *key) { return key; }
void thread_be_background(const char *name) { (void)name; }
void thread_be_realtime(const char *name, int priority) {
	(void)name;
	(void)priority;
}

static int report_fd;
static char directory[128], current_path[256], previous_path[264];

static void check(bool ok, const char *message) {
	if (!ok) {
		dprintf(report_fd, "FAIL: %s (errno=%d)\n", message, errno);
		_exit(1);
	}
}

static off_t size_of(const char *path) {
	struct stat st;
	return stat(path, &st) == 0 ? st.st_size : -1;
}

static void fill(const char *path, char value, size_t size) {
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	check(fd >= 0, "open synthetic log");
	char chunk[8192];
	memset(chunk, value, sizeof(chunk));
	while (size) {
		size_t n = size < sizeof(chunk) ? size : sizeof(chunk);
		check(write(fd, chunk, n) == (ssize_t)n, "write synthetic log");
		size -= n;
	}
	check(close(fd) == 0, "close synthetic log");
}

static void check_bytes(const char *path, off_t offset, const char *text, size_t size) {
	char chunk[128];
	check(size <= sizeof(chunk), "small expected text");
	int fd = open(path, O_RDONLY);
	check(fd >= 0, "open captured log");
	check(pread(fd, chunk, size, offset) == (ssize_t)size && !memcmp(chunk, text, size), "log text preserved");
	close(fd);
}

static void setup(int startup) {
	unsetenv("SONIX_LOG");
	strcpy(directory, "/tmp/sonix-log-test-XXXXXX");
	check(mkdtemp(directory) != NULL, "create private test directory");
	char folder[160];
	snprintf(folder, sizeof(folder), "%s/.local", directory);
	check(mkdir(folder, 0700) == 0, "create log folder");
	snprintf(current_path, sizeof(current_path), "%s/" LOG_FILE_NAME, folder);
	snprintf(previous_path, sizeof(previous_path), "%s.1", current_path);
	if (startup) {
		if (startup == 2) {
			setenv("SONIX_LOG", current_path, 1);
		}
		logging_init();
		logging_attach_sd(directory);
		logging_flush();
	} else {
		pthread_mutex_init(&queue_lock, NULL);
		snprintf(sd_log_path, sizeof(sd_log_path), "%s", current_path);
		check(redirect_to(fopen(current_path, "ae")), "redirect to managed log");
		on_sd = true;
	}
}

static void oversized(void) {
	fill(current_path, 'A', LOG_FILE_MAX + 4096);
	int fd = open(current_path, O_WRONLY);
	check(fd >= 0, "open old log markers");
	check(pwrite(fd, "TAIL-BEGIN", 10, 4096) == 10, "write tail start");
	check(pwrite(fd, "TAIL-END", 8, LOG_FILE_MAX + 4088) == 8, "write tail end");
	close(fd);
}

static void clean(void) {
	// Only files within this test's freshly created private directory.
	DIR *dir = opendir(directory);
	check(dir != NULL, "open test directory");
	closedir(dir);
	char folder[160];
	snprintf(folder, sizeof(folder), "%s/.local", directory);
	dir = opendir(folder);
	struct dirent *entry;
	while (dir && (entry = readdir(dir))) {
		if (entry->d_name[0] == '.')
			continue;
		char path[512];
		snprintf(path, sizeof(path), "%s/%s", folder, entry->d_name);
		check(unlink(path) == 0, "remove test artifact");
	}
	if (dir)
		closedir(dir);
	check(rmdir(folder) == 0 && rmdir(directory) == 0, "remove private test directories");
}

static void run_case(int mode) {
	setup(mode == 8 ? 2 : (mode == 0 || mode == 7 || mode == 9 || mode == 11));
	if (mode == 0) {
		fill(current_path, 'A', LOG_FILE_MAX - 128);
		char text[4097];
		memset(text, 'B', sizeof(text) - 1);
		text[4096] = '\0';
		fprintf(stdout, "%s\n", text);
		logging_flush();
		check(size_of(previous_path) >= LOG_FILE_MAX - 128 && size_of(previous_path) <= LOG_FILE_MAX, "worker rotates previous within limit");
		check(size_of(current_path) > 0 && size_of(current_path) < 8192, "worker continues new file");
		fill(current_path, 'C', LOG_FILE_MAX);
		fprintf(stderr, "newest record\n");
		logging_flush();
		check_bytes(previous_path, 0, "CCCC", 4);
		check(size_of(current_path) <= LOG_FILE_MAX, "second rotation stays bounded");
	} else if (mode >= 1 && mode <= 5) {
		oversized();
		fill(previous_path, 'P', 32);
		fail_temp = mode == 2;
		fail_read = mode == 3;
		fail_rename = mode == 4;
		fail_truncate = mode == 5;
		write_queued(STDOUT_FILENO, "NEW\n", 4);
		if (mode == 1) {
			check(size_of(current_path) == 4 && size_of(previous_path) == LOG_FILE_MAX, "oversized log bounded");
			check_bytes(previous_path, 0, "TAIL-BEGIN", 10);
			check_bytes(previous_path, LOG_FILE_MAX - 8, "TAIL-END", 8);
			check_bytes(current_path, 0, "NEW\n", 4);
		} else {
			check(size_of(current_path) == LOG_FILE_MAX + 4096, "failure leaves original size intact");
			check_bytes(current_path, 4096, "TAIL-BEGIN", 10);
			check_bytes(current_path, LOG_FILE_MAX + 4088, "TAIL-END", 8);
			if (mode != 5)
				check_bytes(previous_path, 0, "PPPP", 4);
			else
				check_bytes(previous_path, LOG_FILE_MAX - 8, "TAIL-END", 8);
			check(queue_dropped == 4, "failure drops new bytes instead of growing file");
		}
	} else if (mode == 6) {
		fill(current_path, 'A', LOG_FILE_MAX);
		fail_rename = true;
		write_queued(STDOUT_FILENO, "DROP\n", 5);
		check(size_of(current_path) == LOG_FILE_MAX && queue_dropped == 5, "read-only rotation stays bounded");
	} else if (mode == 7) {
		fill(current_path, 'A', LOG_FILE_MAX);
		long before = queue_now_ms();
		logging_suspend_for_usb();
		check(queue_now_ms() - before < 1200, "USB drain does not self-wait for 1500 ms");
		check(!on_sd && usb_stream, "USB releases card stream");
		fprintf(stdout, "during USB export\n");
		logging_flush();
		logging_resume_after_usb();
		logging_flush();
		check(on_sd && !usb_stream && !usb_was_on_sd, "USB resumes managed log");
		// Direct held output may cross the cap during handoff; the writer's
		// next sync finishes deferred rotation even without another line.
		usleep(1200000);
		check(size_of(current_path) <= LOG_FILE_MAX && size_of(previous_path) <= LOG_FILE_MAX, "USB resume rotation stays bounded");
	} else if (mode == 8) {
		fill(current_path, 'A', LOG_FILE_MAX);
		check(lseek(STDOUT_FILENO, 0, SEEK_END) == LOG_FILE_MAX, "position explicit output at fixture end");
		fprintf(stdout, "OVERRIDE\n");
		logging_flush();
		check(size_of(current_path) > LOG_FILE_MAX && size_of(previous_path) == -1, "explicit override remains unbounded");
	} else if (mode == 9) {
		fill(current_path, 'A', LOG_FILE_MAX);
		pthread_mutex_lock(&log_lock);
		long before = queue_now_ms();
		fprintf(stdout, "boundary while changing destination\n");
		logging_flush();
		check(queue_now_ms() - before < 1200, "writer never waits for held destination lock");
		check(size_of(current_path) == LOG_FILE_MAX && queue_dropped > 0, "busy rotation drops excess bytes");
		pthread_mutex_unlock(&log_lock);
		fprintf(stdout, "after destination unlocked\n");
		logging_flush();
		check(size_of(current_path) < 1024 && size_of(previous_path) == LOG_FILE_MAX, "rotation recovers when destination unlocks");
	} else if (mode == 10) {
		on_sd = false;
		fill(current_path, 'A', LOG_FILE_MAX);
		write_queued(STDOUT_FILENO, "UNMANAGED\n", 10);
		check(size_of(current_path) == LOG_FILE_MAX + 10 && size_of(previous_path) == -1, "unmanaged output is not rotated or capped");
	} else if (mode == 11 || mode == 12) {
		if (mode == 11) {
			logging_suspend_for_usb();
		} else {
			char path[160];
			snprintf(path, sizeof(path), "%s/held", directory);
			early_stream = open_held(path);
			check(early_stream && redirect_to(early_stream), "hold startup output without writer");
			on_sd = false;
		}
		check(holding_output, "held destination marked");
		char text[4096];
		memset(text, 'H', sizeof(text));
		// Two MiB of ordinary diagnostics must not occupy two MiB of tmpfs.
		for (int i = 0; i < 512; i++) {
			if (mode == 11) {
				fwrite(text, 1, sizeof(text), stdout);
				logging_flush();
			} else {
				write_all(STDOUT_FILENO, text, sizeof(text));
			}
		}
		struct stat st;
		check(fstat(STDOUT_FILENO, &st) == 0 && st.st_size == HELD_MAX, "tmpfs file remains at 32 KiB");
		write_all(STDERR_FILENO, "extra diagnostics\n", 18);
		check(fstat(STDERR_FILENO, &st) == 0 && st.st_size == HELD_MAX, "stderr shares held cap");
		if (mode == 11) {
			logging_resume_after_usb();
			logging_flush();
			check(!holding_output && on_sd, "resume releases held cap");
			off_t before = size_of(current_path);
			fprintf(stdout, "after USB resume\n");
			logging_flush();
			check(size_of(current_path) > before, "normal logging resumes after a full held file");
			check(size_of(current_path) < HELD_MAX + 1024, "only bounded held bytes drained");
		} else {
			open_null_log();
			check(!holding_output, "discarded startup resets held state");
		}
	}
	clean();
}

int main(void) {
	report_fd = dup(STDOUT_FILENO);
	const char *names[] = {"worker rotation", "legacy tail", "read-only legacy", "read failure", "rename failure", "truncate failure", "read-only rotation", "USB handoff", "override", "destination lock contention", "unmanaged output", "USB held tmpfs cap", "startup held tmpfs cap"};
	for (int mode = 0; mode < 13; mode++) {
		pid_t child = fork();
		check(child >= 0, "fork test case");
		if (!child) {
			alarm(15);
			run_case(mode);
			_exit(0);
		}
		int status;
		check(waitpid(child, &status, 0) == child, "wait for test case");
		if (!WIFEXITED(status) || WEXITSTATUS(status))
			return 1;
		dprintf(report_fd, "PASS %s\n", names[mode]);
	}
	return 0;
}
