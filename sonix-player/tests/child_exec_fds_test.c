/*
 * cc -std=gnu11 -O1 -g -DHOST_BUILD -DLV_CONF_INCLUDE_SIMPLE -I. \
 *   -ffunction-sections -fdata-sections tests/child_exec_fds_test.c \
 *   -Wl,--gc-sections -pthread -o /tmp/child_exec_fds_test
 * /tmp/child_exec_fds_test
 * Uses private temporary files/pipes and execs this test; no device access.
 */
#define _GNU_SOURCE
#include "src/system/core/logging.c"
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

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
		assert(close(music) == 0 && close(other) == 0);
	}
	puts("PASS: exec drops inherited track/socket FDs, preserves parent handles and daemon logging after player exit");
	return 0;
}
