#define _GNU_SOURCE
#include "podcastdl.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include "src/system/core/utils.h"
#include "src/system/device/system.h"
#include "src/system/net/http.h"
#include "src/system/streaming/podcaststate.h"

#define DL_TIMEOUT_SECS 20
#define DL_CHUNK 32768
#define DL_NAME_MAX 150 // bytes of a file or folder name, before the extension

typedef struct {
	unsigned state_epoch;
	podcast_episode_t episode;
	char feed_title[PODCAST_TITLE_MAX];
	char feed_author[PODCAST_AUTHOR_MAX];
	char folder[512];
	char path[768];
	char image[PODCAST_URL_MAX];
	char show[DL_NAME_MAX + 1], file[180], staging[240];
	dev_t root_dev, part_dev;
	ino_t root_ino, part_ino;
	bool part_created;
} job_t;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static podcastdl_state_t state = PODCASTDL_IDLE;
static long long bytes_done;
static long long bytes_total;
static _Atomic bool cancel_asked;
static unsigned temp_serial;
static http_stream_t *active_stream; // guarded by `lock`; NULL outside the body loop

bool podcastdl_root(char *out, size_t size) {
	if (!out || !size) {
		return false;
	}
	out[0] = '\0';
	// storage_sd_root's string is fixed at startup; availability can change.
	// This is only a path snapshot, never storage admission or an epoch.
	const char *sd = storage_sd_root();
	if (!sd || !sd[0]) {
		return false;
	}
	int n = snprintf(out, size, "%s/%s", sd, PODCASTDL_FOLDER);
	if (n < 0 || (size_t)n >= size) {
		out[0] = '\0';
		return false;
	}
	return true;
}

// The same mapping as podcastcache.c: the decoder picks its path from the
// extension, and a feed that declares nothing is almost always MP3.
static const char *extension_for(const char *mime) {
	if (!mime || !*mime) {
		return "mp3";
	}
	if (strstr(mime, "mp4") || strstr(mime, "m4a") || strstr(mime, "aac")) {
		return "m4a";
	}
	if (strstr(mime, "ogg") || strstr(mime, "opus")) {
		return "opus";
	}
	if (strstr(mime, "wav")) {
		return "wav";
	}
	if (strstr(mime, "flac")) {
		return "flac";
	}
	return "mp3";
}

void podcastdl_safe_name(const char *title, const char *fallback, char *out, size_t size) {
	if (!out || size == 0) {
		return;
	}
	size_t cap = size - 1 < DL_NAME_MAX ? size - 1 : DL_NAME_MAX;
	size_t n = 0;
	for (const unsigned char *p = (const unsigned char *)(title ? title : ""); *p && n < cap; p++) {
		unsigned char c = *p;
		if (c < 0x20 || strchr("/\\*?\"<>|", c)) {
			continue;
		}
		// "Part 1: The start" becomes "Part 1 - The start", "12:30" becomes "12-30".
		if (c == ':') {
			if (p[1] == ' ' && n + 2 <= cap) {
				out[n++] = ' ';
			}
			c = '-';
		}
		out[n++] = (char)c;
	}
	// A cut inside a multi-byte character leaves the lead bytes of it behind.
	if (n == cap) {
		while (n > 0 && ((unsigned char)out[n - 1] & 0xC0) == 0x80) {
			n--;
		}
		if (n > 0 && ((unsigned char)out[n - 1] & 0x80)) {
			n--;
		}
	}
	out[n] = '\0';

	// FAT drops trailing dots and spaces on its own, which would make the name
	// on the card differ from the one asked for.
	while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '.')) {
		out[--n] = '\0';
	}
	size_t lead = 0;
	while (out[lead] == ' ' || out[lead] == '.') {
		lead++;
	}
	if (lead) {
		memmove(out, out + lead, n - lead + 1);
	}
	if (!out[0]) {
		snprintf(out, size, "%s", fallback ? fallback : "Podcast");
	}
}

static bool folder_of(const char *base, const char *feed_title, char *out, size_t size) {
	char name[DL_NAME_MAX + 1];
	podcastdl_safe_name(feed_title, "Podcast", name, sizeof(name));
	int n = snprintf(out, size, "%s/%s", base, name);
	if (n < 0 || (size_t)n >= size) {
		out[0] = '\0';
		return false;
	}
	return true;
}

static bool path_in_folder(const podcast_episode_t *episode, const char *folder, char *out, size_t size) {
	char fallback[32];
	snprintf(fallback, sizeof(fallback), "%lld", episode->id);
	char name[DL_NAME_MAX + 1];
	podcastdl_safe_name(episode->title, fallback, name, sizeof(name));
	int n = snprintf(out, size, "%s/%s.%s", folder, name, extension_for(episode->mime));
	if (n < 0 || (size_t)n >= size) {
		out[0] = '\0';
		return false;
	}
	return true;
}

bool podcastdl_path(const podcast_episode_t *episode, const char *feed_title, char *out, size_t size) {
	if (out && size) {
		out[0] = '\0';
	}
	if (!episode || !out || !size) {
		return false;
	}
	char base[512], folder[512];
	return podcastdl_root(base, sizeof(base)) && folder_of(base, feed_title && feed_title[0] ? feed_title : episode->feed_title, folder, sizeof(folder)) && path_in_folder(episode, folder, out, size);
}

bool podcastdl_exists(const podcast_episode_t *episode, const char *feed_title) {
	char path[768];
	struct stat st;
	return podcastdl_path(episode, feed_title, path, sizeof(path)) && stat(path, &st) == 0 && st.st_size > 0;
}

static void set_state(podcastdl_state_t s) {
	pthread_mutex_lock(&lock);
	state = s;
	pthread_mutex_unlock(&lock);
}

// No descriptor survives a network wait. A short pin prevents ordinary USB
// unmount during local I/O; relative lookup also survives a forced/lazy detach
// without resolving a replacement card through the old absolute pathname.
static int job_dir(const job_t *job, bool create, bool cleanup) {
	if ((!cleanup && cancel_asked) || job->state_epoch != podcaststate_card_epoch())
		return -1;
	char root[512];
	int pin = storage_pin_card(root, sizeof(root));
	struct stat st;
	if (pin < 0)
		return -1;
	if (job->state_epoch != podcaststate_card_epoch() || fstat(pin, &st) != 0 || st.st_dev != job->root_dev || st.st_ino != job->root_ino || (!cleanup && cancel_asked)) {
		close(pin);
		return -1;
	}
	if (create && mkdirat(pin, PODCASTDL_FOLDER, 0755) != 0 && errno != EEXIST) {
		close(pin);
		return -1;
	}
	int parent = openat(pin, PODCASTDL_FOLDER, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	close(pin);
	if (parent < 0)
		return -1;
	if (create && mkdirat(parent, job->show, 0755) != 0 && errno != EEXIST) {
		close(parent);
		return -1;
	}
	int dir = openat(parent, job->show, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	close(parent);
	return dir;
}

static bool same_part(const job_t *job, int fd) {
	struct stat st;
	return fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_dev == job->part_dev && st.st_ino == job->part_ino;
}

static bool create_part(job_t *job) {
	int dir = job_dir(job, true, false);
	if (dir < 0)
		return false;
	int fd = -1;
	for (int attempt = 0; attempt < 16; attempt++) {
		unsigned serial = __atomic_add_fetch(&temp_serial, 1, __ATOMIC_RELAXED);
		snprintf(job->staging, sizeof(job->staging), ".%s-%ld-%u.part", job->file, (long)getpid(), serial);
		fd = openat(dir, job->staging, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
		if (fd >= 0 || errno != EEXIST)
			break;
	}
	struct stat st;
	bool ok = fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode);
	if (ok) {
		job->part_dev = st.st_dev;
		job->part_ino = st.st_ino;
		job->part_created = true;
	}
	if (fd >= 0) {
		if (!ok)
			unlinkat(dir, job->staging, 0);
		close(fd);
	}
	close(dir);
	return ok;
}

static bool append_part(job_t *job, const char *buf, size_t size, long long have) {
	int dir = job_dir(job, false, false);
	if (dir < 0)
		return false;
	int fd = openat(dir, job->staging, O_WRONLY | O_APPEND | O_NOFOLLOW | O_CLOEXEC);
	struct stat st;
	bool ok = same_part(job, fd) && fstat(fd, &st) == 0 && st.st_size == have;
	close(dir);
	while (ok && size) {
		ssize_t n = write(fd, buf, size);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0) {
			ok = false;
			break;
		}
		buf += n;
		size -= (size_t)n;
	}
	if (fd >= 0 && close(fd) != 0)
		ok = false;
	return ok;
}

static void cleanup_part(const job_t *job) {
	if (!job->part_created)
		return;
	int dir = job_dir(job, false, true);
	if (dir < 0)
		return;
	int fd = openat(dir, job->staging, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (same_part(job, fd))
		unlinkat(dir, job->staging, 0);
	if (fd >= 0)
		close(fd);
	close(dir);
}

static bool download_body(job_t *job) {
	http_stream_t *st = calloc(1, sizeof(*st));
	if (!st)
		return false;
	if (!http_stream_open(st, job->episode.enclosure, DL_TIMEOUT_SECS)) {
		free(st);
		return false;
	}
	pthread_mutex_lock(&lock);
	bytes_total = st->content_length > 0 ? st->content_length : 0;
	active_stream = st;
	if (cancel_asked)
		http_stream_wake(st);
	pthread_mutex_unlock(&lock);

	char *buf = malloc(DL_CHUNK);
	bool ok = buf && create_part(job);
	long long done = 0;
	while (ok && !cancel_asked) {
		int n = http_stream_read(st, buf, DL_CHUNK);
		if (n < 0) {
			ok = false;
			break;
		}
		if (!n)
			break;
		ok = append_part(job, buf, (size_t)n, done);
		if (ok) {
			done += n;
			pthread_mutex_lock(&lock);
			bytes_done = done;
			pthread_mutex_unlock(&lock);
		}
	}
	free(buf);
	ok = ok && !cancel_asked && done > 0 && (st->content_length <= 0 || done == st->content_length);
	pthread_mutex_lock(&lock);
	active_stream = NULL;
	pthread_mutex_unlock(&lock);
	http_stream_close(st);
	free(st);
	return ok;
}

// Artwork is optional. Fetch without a card pin, then admit a fresh local
// transaction; cancellation/detach cannot turn its late response into a write.
static void fetch_cover(const job_t *job) {
	if (!job->image[0] || cancel_asked)
		return;
	int dir = job_dir(job, false, false);
	if (dir < 0)
		return;
	struct stat st;
	bool exists = fstatat(dir, "cover.jpg", &st, AT_SYMLINK_NOFOLLOW) == 0 || fstatat(dir, "cover.png", &st, AT_SYMLINK_NOFOLLOW) == 0;
	close(dir);
	if (exists)
		return;
	char *body = NULL;
	size_t len = 0;
	if (!http_get(job->image, &body, &len, 8 * 1024 * 1024, DL_TIMEOUT_SECS) || !body || len < 8) {
		free(body);
		return;
	}
	const unsigned char *b = (const unsigned char *)body;
	const char *dest = b[0] == 0xFF && b[1] == 0xD8 ? "cover.jpg" : b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G' ? "cover.png" : NULL;
	dir = dest ? job_dir(job, false, false) : -1;
	if (dir >= 0) {
		char tmp[80];
		snprintf(tmp, sizeof(tmp), ".cover-%ld-%u.tmp", (long)getpid(), __atomic_add_fetch(&temp_serial, 1, __ATOMIC_RELAXED));
		int fd = openat(dir, tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
		if (fd >= 0) {
			FILE *f = fdopen(fd, "wb");
			bool ok = f && fwrite(body, 1, len, f) == len && fflush(f) == 0 && fsync(fileno(f)) == 0;
			if (f ? fclose(f) != 0 : close(fd) != 0)
				ok = false;
			if (!ok || cancel_asked || job->state_epoch != podcaststate_card_epoch() || renameat(dir, tmp, dir, dest) != 0)
				unlinkat(dir, tmp, 0);
			else
				fsync(dir);
		}
		close(dir);
	}
	free(body);
}

// The sidecar podcastcache.c writes for an episode it streams, with the id of
// the episode added: here the file name is its title, not its id.
static bool prepare_tags(const job_t *job, int dir, char *tmp, size_t size) {
	char tags[200];
	snprintf(tags, sizeof(tags), "%s.tags", job->file);
	snprintf(tmp, size, ".%s-%ld-%u.tmp", tags, (long)getpid(), __atomic_add_fetch(&temp_serial, 1, __ATOMIC_RELAXED));
	int fd = openat(dir, tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	FILE *f = fd >= 0 ? fdopen(fd, "w") : NULL;
	if (!f) {
		if (fd >= 0) {
			close(fd);
			unlinkat(dir, tmp, 0);
		}
		tmp[0] = 0;
		return false;
	}
	const podcast_episode_t *e = &job->episode;
	fprintf(f, "title=%s\n", e->title);
	fprintf(f, "artist=%s\n", job->feed_title);
	fprintf(f, "album=%s\n", job->feed_title);
	fprintf(f, "feed_id=%lld\n", e->feed_id);
	fprintf(f, "feed_author=%s\n", job->feed_author);
	fprintf(f, "feed_image=%s\n", job->image);
	fprintf(f, "cover_url=%s\n", e->image);
	fprintf(f, "episode_id=%lld\n", e->id);
	bool ok = !ferror(f) && fflush(f) == 0 && fsync(fileno(f)) == 0;
	if (fclose(f) != 0)
		ok = false;
	if (!ok)
		unlinkat(dir, tmp, 0);
	return ok;
}

static void *download_main(void *arg) {
	job_t *job = arg;
	thread_be_low_priority("podcast save"); // the card on screen is waiting for it

	bool ok = !cancel_asked && download_body(job);
	int dir = ok ? job_dir(job, false, false) : -1;
	int fd = dir >= 0 ? openat(dir, job->staging, O_RDONLY | O_NOFOLLOW | O_CLOEXEC) : -1;
	ok = ok && same_part(job, fd);
	if (ok && job->episode.published > 0) {
		struct timespec times[2] = {{.tv_sec = job->episode.published}, {.tv_sec = job->episode.published}};
		ok = futimens(fd, times) == 0;
	}
	char tags_tmp[240] = "", tags[200];
	snprintf(tags, sizeof(tags), "%s.tags", job->file);
	if (ok)
		ok = fsync(fd) == 0 && prepare_tags(job, dir, tags_tmp, sizeof(tags_tmp));
	if (ok)
		ok = !cancel_asked && job->state_epoch == podcaststate_card_epoch() && renameat(dir, job->staging, dir, job->file) == 0;
	if (ok)
		ok = renameat(dir, tags_tmp, dir, tags) == 0;
	if (tags_tmp[0] && dir >= 0)
		unlinkat(dir, tags_tmp, 0);
	// Unsupported directory fsync is normal on some filesystems; real I/O
	// failure must not be announced as a completed download.
	if (ok && fsync(dir) != 0 && errno != EINVAL && errno != ENOTSUP)
		ok = false;
	if (fd >= 0)
		close(fd);
	if (dir >= 0)
		close(dir);
	if (!ok) {
		cleanup_part(job);
	} else {
		podcaststate_downloaded(job->episode.id, job->path, job->state_epoch);
		fetch_cover(job);
	}

	printf("podcastdl: %s %s\n", job->path, ok ? "complete" : cancel_asked ? "cancelled" : "failed");
	set_state(ok ? PODCASTDL_DONE : cancel_asked ? PODCASTDL_CANCELLED : PODCASTDL_FAILED);
	free(job);
	return NULL;
}

bool podcastdl_owns(const char *path) {
	char base[512];
	if (!path || !podcastdl_root(base, sizeof(base))) {
		return false;
	}
	size_t n = strlen(base);
	return strncasecmp(path, base, n) == 0 && path[n] == '/';
}

bool podcastdl_start(const podcast_episode_t *episode, const char *feed_title, const char *feed_author, const char *feed_image) {
	if (!episode || !episode->enclosure[0]) {
		return false;
	}
	pthread_mutex_lock(&lock);
	bool busy = state == PODCASTDL_RUNNING;
	if (!busy) {
		state = PODCASTDL_RUNNING;
		bytes_done = bytes_total = 0;
		cancel_asked = false;
	}
	pthread_mutex_unlock(&lock);
	if (busy) {
		return false;
	}

	job_t *job = calloc(1, sizeof(*job));
	if (!job) {
		set_state(PODCASTDL_FAILED);
		return false;
	}
	job->state_epoch = podcaststate_card_epoch();
	job->episode = *episode;
	const char *title = feed_title && feed_title[0] ? feed_title : episode->feed_title;
	snprintf(job->feed_title, sizeof(job->feed_title), "%s", title);
	snprintf(job->feed_author, sizeof(job->feed_author), "%s", feed_author ? feed_author : "");
	char root[512], base[512];
	int pin = storage_pin_card(root, sizeof(root));
	struct stat st;
	bool admitted = pin >= 0 && fstat(pin, &st) == 0 && job->state_epoch == podcaststate_card_epoch();
	if (pin >= 0)
		close(pin);
	int n = admitted ? snprintf(base, sizeof(base), "%s/" PODCASTDL_FOLDER, root) : -1;
	if (n < 0 || (size_t)n >= sizeof(base) || !folder_of(base, title, job->folder, sizeof(job->folder)) || !path_in_folder(episode, job->folder, job->path, sizeof(job->path))) {
		free(job);
		set_state(PODCASTDL_FAILED);
		return false;
	}
	job->root_dev = st.st_dev;
	job->root_ino = st.st_ino;
	podcastdl_safe_name(title, "Podcast", job->show, sizeof(job->show));
	snprintf(job->file, sizeof(job->file), "%s", strrchr(job->path, '/') + 1);
	snprintf(job->image, sizeof(job->image), "%s", feed_image && feed_image[0] ? feed_image : episode->image);

	pthread_t thread;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_attr_setstacksize(&attr, 256 * 1024);
	bool started = pthread_create(&thread, &attr, download_main, job) == 0;
	pthread_attr_destroy(&attr);
	if (!started) {
		free(job);
		set_state(PODCASTDL_FAILED);
		return false;
	}
	return true;
}

void podcastdl_cancel(void) {
	cancel_asked = true;
	// A read blocked on a stalled socket returns only at the receive timeout
	// unless the socket is shut under it.
	pthread_mutex_lock(&lock);
	if (active_stream) {
		http_stream_wake(active_stream);
	}
	pthread_mutex_unlock(&lock);
}

podcastdl_state_t podcastdl_state(long long *done, long long *total) {
	pthread_mutex_lock(&lock);
	podcastdl_state_t s = state;
	if (done) {
		*done = bytes_done;
	}
	if (total) {
		*total = bytes_total;
	}
	pthread_mutex_unlock(&lock);
	return s;
}

void podcastdl_acknowledge(void) {
	pthread_mutex_lock(&lock);
	if (state != PODCASTDL_RUNNING) {
		state = PODCASTDL_IDLE;
	}
	pthread_mutex_unlock(&lock);
}

bool podcastdl_busy(void) { return podcastdl_state(NULL, NULL) == PODCASTDL_RUNNING; }
