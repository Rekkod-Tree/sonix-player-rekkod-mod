/*
 * cc -std=gnu11 -O1 -g -DHOST_BUILD -DLV_CONF_INCLUDE_SIMPLE -I. \
 *   -ffunction-sections -fdata-sections tests/child_exec_fds_test.c \
 *   -Wl,--gc-sections -pthread -o /tmp/child_exec_fds_test
 * /tmp/child_exec_fds_test
 * Uses private temporary files/pipes and execs this test; no device access.
 */
#define _GNU_SOURCE
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>
static int fixture_fstat(int fd, struct stat *st);
static int fixture_fcntl(int fd, int command, ...);
static int fixture_close(int fd);
#define fstat fixture_fstat
#define fcntl fixture_fcntl
#define close fixture_close
#include "src/system/core/logging.c"
#undef fstat
#undef fcntl
#undef close
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

enum { NORMAL, FCNTL_EINTR, STAT_EINTR, STAT_EBADF, STAT_ERROR, FCNTL_ERROR, CLOSE_EINTR, CLOSE_EBADF, CLOSE_ERROR };
static int sound_fd = -1, regular_fd = -1, injection, stat_calls, fcntl_calls, close_calls;

static int fixture_fcntl(int fd, int command, ...) {
	int argument = 0;
	if (command != F_GETFL && command != F_GETFD) {
		va_list args; va_start(args, command); argument = va_arg(args, int); va_end(args);
	}
	if (fd == sound_fd && command == F_SETFD) {
		fcntl_calls++;
		if (injection == FCNTL_ERROR || (injection == FCNTL_EINTR && fcntl_calls == 1)) {
			errno = injection == FCNTL_ERROR ? EIO : EINTR; return -1;
		}
	}
	return fcntl(fd, command, argument);
}

static int fixture_fstat(int fd, struct stat *st) {
	if (fd == sound_fd) {
		stat_calls++;
		if (injection == STAT_ERROR || (injection == STAT_EINTR && stat_calls == 1)) {
			errno = injection == STAT_ERROR ? EIO : EINTR; return -1;
		}
		if (injection == STAT_EBADF) { close(fd); errno = EBADF; return -1; }
	}
	int rc = fstat(fd, st);
	if (!rc && fd == sound_fd) { st->st_mode = S_IFCHR | 0600; st->st_rdev = makedev(116, 16); }
	// A regular file with matching rdev still must not be treated as sound.
	if (!rc && fd == regular_fd) st->st_rdev = makedev(116, 16);
	return rc;
}

static int fixture_close(int fd) {
	int rc = close(fd);
	if (fd == sound_fd) {
		close_calls++;
		if (injection == CLOSE_EINTR || injection == CLOSE_EBADF || injection == CLOSE_ERROR) {
			errno = injection == CLOSE_EINTR ? EINTR : injection == CLOSE_EBADF ? EBADF : EIO;
			return -1;
		}
	}
	return rc;
}

static void check_point_fix(int music, int other, int character, const int pipes[2]) {
	errno = 0;
	assert(fcntl(sound_fd, F_GETFD) < 0 && errno == EBADF);
	assert(close_calls == (injection == STAT_EBADF ? 0 : 1));
	assert(fcntl_calls == (injection == FCNTL_EINTR ? 2 : 1));
	assert(stat_calls == (injection == STAT_EINTR ? 2 : 1));
	assert(fcntl(music, F_GETFD) == FD_CLOEXEC);
	assert(fcntl(other, F_GETFD) == FD_CLOEXEC);
	assert(fcntl(character, F_GETFD) == FD_CLOEXEC);
	if (pipes[0] >= 0) {
		assert(fcntl(pipes[0], F_GETFD) == FD_CLOEXEC);
		assert(fcntl(pipes[1], F_GETFD) == FD_CLOEXEC);
	}
}

static void probe(int music, int other) {
	errno = 0;
	assert(fcntl(music, F_GETFD) < 0 && errno == EBADF);
	errno = 0;
	assert(fcntl(other, F_GETFD) < 0 && errno == EBADF);
	// The retained private relay reader prevents SIGPIPE after the player
	// exits. The child's stdio must remain usable across exec.
	assert(write(STDOUT_FILENO, "ok\n", 3) == 3);
	assert(write(STDERR_FILENO, "ok\n", 3) == 3);
}

int main(int argc, char **argv) {
	if (argc == 4 && !strcmp(argv[1], "--probe")) {
		probe(atoi(argv[2]), atoi(argv[3]));
		return 0;
	}
	assert(argc == 1);
	signal(SIGPIPE, SIG_DFL);
	child_fd_limit_init();
	assert(child_fd_limit > 3);
	for (int logging = 0; logging < 3; logging++) {
		char path[] = "/tmp/sonix-child-exec-XXXXXX";
		int music = mkstemp(path);
		assert(music >= 3 && unlink(path) == 0);
		int other = socket(AF_UNIX, SOCK_STREAM, 0);
		assert(other > music);
		regular_fd = music;
		int character = open("/dev/null", O_RDWR);
		assert(character > other);
		sound_fd = fcntl(character, F_DUPFD, 200);
		assert(sound_fd >= 200);
		assert(fcntl(music, F_GETFD) == 0 && fcntl(other, F_GETFD) == 0);
		int pipes[2] = {-1, -1};
		int capture[2] = {-1, -1};
		if (logging) {
			assert(pipe(pipes) == 0);
			relay_read = pipes[0];
			relay_write = pipes[1];
		} else {
			relay_read = relay_write = -1;
		}
		if (logging == 2) {
			assert(pipe(capture) == 0);
		}
		char first[24], second[24];
		snprintf(first, sizeof(first), "%d", music);
		snprintf(second, sizeof(second), "%d", other);
		pid_t child = fork();
		assert(child >= 0);
		if (!child) {
			logging_child_stdio();
			check_point_fix(music, other, character, pipes);
			if (logging == 2) {
				assert(dup2(capture[1], STDOUT_FILENO) == STDOUT_FILENO);
				close(capture[0]);
				close(capture[1]);
			}
			if (logging) {
				close(pipes[0]);
				close(pipes[1]);
			}
			execl("/proc/self/exe", argv[0], "--probe", first, second, (char *)NULL);
			_exit(127);
		}
		if (logging) {
			close(pipes[0]);
			close(pipes[1]);
		}
		if (logging == 2) {
			close(capture[1]);
			char answer[3];
			assert(read(capture[0], answer, sizeof(answer)) == sizeof(answer));
			assert(!memcmp(answer, "ok\n", sizeof(answer)));
			close(capture[0]);
		}
		int status;
		assert(waitpid(child, &status, 0) == child);
		assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
		assert(fcntl(music, F_GETFD) == 0 && fcntl(other, F_GETFD) == 0);
		assert(fcntl(sound_fd, F_GETFD) == 0 && fcntl(character, F_GETFD) == 0);
		assert(close(sound_fd) == 0 && close(character) == 0 && close(music) == 0 && close(other) == 0);
	}
	// Failure injection never needs a real ALSA node; only fstat's classification
	// is synthetic. Each parent retains its actual harmless descriptor and flags.
	for (injection = FCNTL_EINTR; injection <= CLOSE_ERROR; injection++) {
		int harmless = open("/dev/null", O_RDWR);
		assert(harmless >= 3);
		sound_fd = fcntl(harmless, F_DUPFD, 200);
		assert(sound_fd >= 200);
		close(harmless);
		relay_read = relay_write = regular_fd = -1;
		stat_calls = fcntl_calls = close_calls = 0;
		pid_t child = fork(); assert(child >= 0);
		if (!child) {
			logging_child_stdio();
			if (injection == STAT_ERROR || injection == FCNTL_ERROR || injection == CLOSE_ERROR) _exit(99);
			assert(fcntl(sound_fd, F_GETFD) < 0 && errno == EBADF);
			assert(close_calls == (injection == STAT_EBADF ? 0 : 1));
			assert(fcntl_calls == (injection == FCNTL_EINTR ? 2 : 1));
			if (injection != FCNTL_ERROR) assert(stat_calls == (injection == STAT_EINTR ? 2 : 1));
			_exit(0);
		}
		int status; assert(waitpid(child, &status, 0) == child && WIFEXITED(status));
		assert(WEXITSTATUS(status) == (injection == STAT_ERROR || injection == FCNTL_ERROR || injection == CLOSE_ERROR ? 127 : 0));
		assert(fcntl(sound_fd, F_GETFD) == 0);
		close(sound_fd);
	}
	puts("PASS: exec drops inherited track/socket FDs, preserves parent handles and daemon logging after player exit");
	return 0;
}
