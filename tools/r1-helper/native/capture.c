// One active RGB565 frame over stdout. Never write the card or framebuffer.
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>

static int write_all(int fd, const void *data, size_t size) {
	const unsigned char *p = data;
	while (size) {
		ssize_t n = write(fd, p, size);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) return -1;
		p += n;
		size -= (size_t)n;
	}
	return 0;
}

int main(void) {
	int fd = open("/dev/fb0", O_RDONLY);
	struct fb_var_screeninfo v, after;
	struct fb_fix_screeninfo f;
	unsigned char *pixels = NULL;
	unsigned char *mapping = MAP_FAILED;
	int result = 1;
	if (fd < 0) { perror("framebuffer"); return 1; }
	if (ioctl(fd, FBIOGET_FSCREENINFO, &f) < 0) goto done;
	mapping = mmap(NULL, f.smem_len, PROT_READ, MAP_SHARED, fd, 0);
	if (mapping == MAP_FAILED) { perror("framebuffer mmap"); goto done; }
	pixels = malloc(480 * 800 * 2);
	if (!pixels) goto done;
	for (int attempt = 0; attempt < 5; attempt++) {
		if (ioctl(fd, FBIOGET_VSCREENINFO, &v) < 0) goto done;
		if (v.xres != 480 || v.yres != 800 || v.bits_per_pixel != 16 ||
			v.red.offset != 11 || v.red.length != 5 || v.green.offset != 5 ||
			v.green.length != 6 || v.blue.offset != 0 || v.blue.length != 5 ||
			v.xoffset + v.xres > v.xres_virtual || v.yoffset + v.yres > v.yres_virtual)
			goto done;
		for (unsigned y = 0; y < v.yres; y++) {
			uint64_t offset = (uint64_t)(v.yoffset + y) * f.line_length + v.xoffset * 2;
			if (offset + v.xres * 2 > f.smem_len) goto done;
			memcpy(pixels + y * v.xres * 2, mapping + offset, v.xres * 2);
		}
		if (ioctl(fd, FBIOGET_VSCREENINFO, &after) < 0) goto done;
		if (v.xoffset != after.xoffset || v.yoffset != after.yoffset) continue;
		char header[160];
		int count = snprintf(header, sizeof(header),
			"{\"width\":480,\"height\":800,\"format\":\"rgb565le\",\"yoffset\":%u,\"stride\":%u}\n",
			v.yoffset, f.line_length);
		if (write_all(STDOUT_FILENO, header, (size_t)count) ||
			write_all(STDOUT_FILENO, pixels, 480 * 800 * 2)) goto done;
		result = 0;
		break;
	}
done:
	if (result) fprintf(stderr, "Cannot read a supported, stable R1 framebuffer\n");
	free(pixels);
	if (mapping != MAP_FAILED) munmap(mapping, f.smem_len);
	close(fd);
	return result;
}
