// R1 touch only. Validate every argument before pressing; always release.
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

static volatile sig_atomic_t stopped;
static void stop(int sig) { (void)sig; stopped = 1; }

static int event(int fd, unsigned short type, unsigned short code, int value) {
	struct input_event e = {.type = type, .code = code, .value = value};
	return write(fd, &e, sizeof(e)) == sizeof(e) ? 0 : -1;
}

static int sync_touch(int fd) {
	int a = event(fd, EV_SYN, SYN_MT_REPORT, 0);
	int b = event(fd, EV_SYN, SYN_REPORT, 0);
	return a || b;
}

static int number(const char *text, int max) {
	char *end;
	errno = 0;
	long n = strtol(text, &end, 10);
	if (!*text || *end || errno || n < 0 || n > max) return -1;
	return (int)n;
}

int main(int argc, char **argv) {
	if (argc != 4 && argc != 6) return 2;
	int x = number(argv[1], 479), y = number(argv[2], 799);
	int end_x = argc == 6 ? number(argv[3], 479) : x;
	int end_y = argc == 6 ? number(argv[4], 799) : y;
	int duration = number(argv[argc - 1], 2000);
	if (x < 0 || y < 0 || end_x < 0 || end_y < 0 || duration < 30) return 2;
	int fd = open("/dev/input/event1", O_WRONLY);
	if (fd < 0) { perror("touch"); return 1; }
	struct input_absinfo axis;
	if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &axis) < 0 || axis.minimum != 0 || axis.maximum != 479 ||
		ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &axis) < 0 || axis.minimum != 0 || axis.maximum != 799) {
		fprintf(stderr, "Unsupported touch coordinates\n"); close(fd); return 2;
	}
	signal(SIGINT, stop);
	signal(SIGTERM, stop);
	int error = event(fd, EV_ABS, ABS_MT_TRACKING_ID, 41);
	error |= event(fd, EV_ABS, ABS_MT_POSITION_X, x);
	error |= event(fd, EV_ABS, ABS_MT_POSITION_Y, y);
	error |= event(fd, EV_ABS, ABS_MT_TOUCH_MAJOR, 8);
	error |= event(fd, EV_ABS, ABS_MT_PRESSURE, 32);
	error |= event(fd, EV_KEY, BTN_TOUCH, 1);
	error |= sync_touch(fd);
	for (int i = 1; !error && !stopped && i <= 20; i++) {
		usleep((unsigned)duration * 1000 / 20);
		if (argc == 6) {
			error |= event(fd, EV_ABS, ABS_MT_POSITION_X, x + (end_x - x) * i / 20);
			error |= event(fd, EV_ABS, ABS_MT_POSITION_Y, y + (end_y - y) * i / 20);
			error |= sync_touch(fd);
		}
	}
	error |= event(fd, EV_ABS, ABS_MT_TRACKING_ID, -1);
	error |= event(fd, EV_KEY, BTN_TOUCH, 0);
	error |= sync_touch(fd);
	close(fd);
	return error || stopped ? 1 : 0;
}
