// cc -std=gnu11 -O1 -Wall -I. -ffunction-sections -fdata-sections tests/podcastrss_test.c -Wl,--gc-sections -pthread -o /tmp/podcastrss_test
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool refuse_alloc;
static void *fixture_realloc(void *p, size_t bytes) { return refuse_alloc ? NULL : realloc(p, bytes); }
#define realloc fixture_realloc
#include "src/system/streaming/podcastrss.c"
#undef realloc

static const char RSS[] = "<rss><channel><title>Road &amp; music</title><itunes:author>Author</itunes:author>"
						  "<item><title>First</title><guid>one</guid><enclosure url=\"https://example.org/1.mp3\" type=\"audio/mpeg\"/>"
						  "<pubDate>Mon, 12 Oct 2026 10:00:00 GMT</pubDate><itunes:duration>01:02:03</itunes:duration></item>"
						  "<item><title>Second</title><guid>two</guid><enclosure url=\"https://example.org/2.mp3\"/>"
						  "<pubDate>Tue, 13 Oct 2026 10:00:00 GMT</pubDate><itunes:duration>90</itunes:duration></item>"
						  "<item><title>No audio</title></item></channel></rss>";

int main(void) {
	parsed_t feed;
	assert(parse("https://example.org/rss", RSS, strlen(RSS), &feed));
	assert(strcmp(feed.feed.title, "Road & music") == 0);
	assert(feed.count == 2);
	assert(strcmp(feed.episodes[0].title, "Second") == 0);
	assert(feed.episodes[0].duration_secs == 90);
	assert(feed.episodes[1].duration_secs == 3723);
	long long id = feed.episodes[0].id;
	free(feed.episodes);
	assert(parse("https://example.org/rss", RSS, strlen(RSS), &feed));
	assert(feed.episodes[0].id == id);
	free(feed.episodes);
	assert(!parse("https://example.org/rss", "<html>offline</html>", 20, &feed));
	refuse_alloc = true;
	assert(!parse("https://example.org/rss", RSS, strlen(RSS), &feed));
	free(feed.episodes);
	puts("PASS RSS names, ordering, durations, stable IDs, missing audio and allocation refusal");
}
