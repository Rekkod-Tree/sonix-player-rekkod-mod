// Bounded, silent framebuffer recording to /tmp. No input events or SD writes.
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define PIXELS (480 * 800)
#define LIMIT (3U * 1024U * 1024U)
static uint64_t milliseconds(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static int number(const char *s, int max) {
	char *end;
	long v = strtol(s, &end, 10);
	return !*s || *end || v < 1 || v > max ? -1 : (int)v;
}
static size_t encode(const uint16_t *now, const uint16_t *old, uint16_t *out) {
	size_t at = 0, n = 0;
	while (at < PIXELS) {
		size_t start = at;
		if (now[at] == old[at]) {
			while (at < PIXELS && now[at] == old[at] && at - start < 32768) at++;
			out[n++] = (uint16_t)(at - start - 1);
		} else if (at + 2 < PIXELS && now[at] == now[at + 1] && now[at] == now[at + 2]) {
			while (at < PIXELS && now[at] == now[start] && at - start < 16384) at++;
			out[n++] = 0x8000 | (uint16_t)(at - start - 1);
			out[n++] = now[start];
		} else {
			at++;
			while (at < PIXELS && at - start < 16384 && now[at] != old[at] &&
				   !(at + 2 < PIXELS && now[at] == now[at + 1] && now[at] == now[at + 2])) at++;
			out[n++] = 0xc000 | (uint16_t)(at - start - 1);
			memcpy(out + n, now + start, (at - start) * 2);
			n += at - start;
		}
	}
	return n;
}
int main(int argc, char **argv) {
	if (argc != 4) return 2;
	int seconds = number(argv[1], 12), fps = number(argv[2], 12);
	const char *prefix = "/tmp/sonix-r1-video-";
	if (seconds < 0 || fps < 0 || strncmp(argv[3], prefix, strlen(prefix)) || strchr(argv[3] + strlen(prefix), '/')) return 2;
	int fd = open("/dev/fb0", O_RDONLY);
	if (fd < 0) return 1;
	struct fb_fix_screeninfo f;
	struct fb_var_screeninfo v, after;
	if (ioctl(fd, FBIOGET_FSCREENINFO, &f) < 0) { close(fd); return 1; }
	unsigned char *mapping = mmap(NULL, f.smem_len, PROT_READ, MAP_SHARED, fd, 0);
	uint16_t *old = calloc(PIXELS, 2), *now = malloc(PIXELS * 2), *packed = malloc(PIXELS * 4);
	FILE *out = NULL;
	int result = 1, count = 0;
	size_t total = 16;
	if (mapping == MAP_FAILED || !old || !now || !packed) goto done;
	int output_fd = open(argv[3], O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (output_fd < 0) goto done;
	out = fdopen(output_fd, "wb");
	if (!out) { close(output_fd); goto done; }
	const uint32_t header[] = {480, 800};
	if (fwrite("SNXR1V1\n", 1, 8, out) != 8 || fwrite(header, 4, 2, out) != 2) goto done;
	uint64_t start = milliseconds();
	for (int frame = 0; frame < seconds * fps; frame++) {
		uint64_t target = start + (uint64_t)frame * 1000 / fps, t = milliseconds();
		if (t < target) usleep((useconds_t)(target - t) * 1000);
		int valid = 0;
		for (int retry = 0; retry < 4; retry++) {
			if (ioctl(fd, FBIOGET_VSCREENINFO, &v) < 0) goto done;
			if (v.xres != 480 || v.yres != 800 || v.bits_per_pixel != 16) goto done;
			for (unsigned y = 0; y < 800; y++) {
				uint64_t offset = (uint64_t)(v.yoffset + y) * f.line_length + v.xoffset * 2;
				if (offset + 960 > f.smem_len) goto done;
				memcpy(now + y * 480, mapping + offset, 960);
			}
			if (ioctl(fd, FBIOGET_VSCREENINFO, &after) < 0) goto done;
			if (v.yoffset == after.yoffset && v.xoffset == after.xoffset) { valid = 1; break; }
		}
		if (!valid) continue;
		size_t n = encode(now, old, packed);
		if (total + 8 + n * 2 > LIMIT) { result = 3; goto done; }
		uint32_t info[] = {(uint32_t)(milliseconds() - start), (uint32_t)(n * 2)};
		if (fwrite(info, 4, 2, out) != 2 || fwrite(packed, 2, n, out) != n) goto done;
		total += 8 + n * 2;
		memcpy(old, now, PIXELS * 2);
		count++;
	}
	result = 0;
done:
	if (out && fclose(out) != 0) result = 1;
	if (mapping != MAP_FAILED) munmap(mapping, f.smem_len);
	close(fd);
	free(old); free(now); free(packed);
	fprintf(stderr, "recorded=%d bytes=%u status=%d cap=%u\n", count, (unsigned)total, result, LIMIT);
	return result;
}
