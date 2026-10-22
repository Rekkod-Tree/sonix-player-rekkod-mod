// From sonix-player:
// cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -ffunction-sections -fdata-sections -I. tests/ota_release_test.c
// src/system/core/json.c -Wl,--gc-sections -pthread -o /tmp/ota_release_test
// /tmp/ota_release_test
// No network, card, firmware installation or device operations: HTTP is stubbed.
#define _GNU_SOURCE
#include <stdbool.h>
#include <stdlib.h>

static bool fail_body_alloc;
static int body_allocations;

static void *fixture_body_malloc(size_t size) {
	body_allocations++;
	return fail_body_alloc ? NULL : malloc(size);
}

#define malloc fixture_body_malloc
#include "src/system/device/ota.c"
#undef malloc
#include <assert.h>

#define parse_version legacy_parse_version
#define version_cmp legacy_version_cmp
#include "ota_legacy_version_3f0bc30.inc"
#undef version_cmp
#undef parse_version

static const char *installed_version = "1.3.0-ui3-alpha25";
static wifi_state_t wifi_state = WIFI_STATE_CONNECTED;
static bool fetch_ok = true;
static int fetch_status = 200, fetch_count, checks;
static const char *fetch_body = "[]";

const char *sysinfo_os_version(void) { return installed_version; }

void wifi_get_status(wifi_status_t *out) {
	memset(out, 0, sizeof(*out));
	out->state = wifi_state;
}

const char *http_last_error(void) { return "fixture"; }

bool http_get_ex(const char *url, const char *headers, bool error_body, char **out, size_t *out_len, size_t limit, int timeout, int *status) {
	fetch_count++;
	assert(strcmp(url, "https://api.github.com/repos/Rekkod-Tree/sonix-player-rekkod-mod/releases?per_page=20") == 0);
	assert(strstr(headers, "application/vnd.github+json") && strstr(headers, "X-GitHub-Api-Version:"));
	assert(!error_body && limit == LIST_LIMIT && timeout == LIST_TIMEOUT_SECS);
	*status = fetch_status;
	if (!fetch_ok) {
		return false;
	}
	*out = strdup(fetch_body);
	assert(*out);
	*out_len = strlen(*out);
	return true;
}

static void compare(const char *left, const char *right, int expected) {
	ota_version_t a, b;
	assert(parse_version(left, &a) && parse_version(right, &b));
	int actual = version_cmp(&a, &b);
	assert(actual == expected);
	assert(version_cmp(&b, &a) == -expected);
	checks++;
}

static void invalid_version(const char *value) {
	ota_version_t version;
	assert(!parse_version(value, &version));
	checks++;
}

typedef struct {
	const char *tag, *name, *body, *asset, *asset_state, *url, *digest;
	long size;
	bool draft, prerelease;
} fixture_t;

static const fixture_t standard = {
	.tag = "r2",
	.name = "v1.2",
	.body = "Sonix-Version: 1.3.0-ui3-alpha26",
	.asset = "r1.upt",
	.asset_state = "uploaded",
	.url = "https://github.com/Rekkod-Tree/sonix-player-rekkod-mod/releases/download/r2/r1.upt",
	.digest = "sha256:2f34d80b151054f154b9c244667a53302598afb861bc21d0e5d791557d5f7558",
	.size = 49780736,
};

static void string(FILE *out, const char *text) {
	if (!text) {
		fputs("null", out);
		return;
	}
	fputc('"', out);
	for (; *text; text++) {
		switch (*text) {
		case '\\':
			fputs("\\\\", out);
			break;
		case '"':
			fputs("\\\"", out);
			break;
		case '\n':
			fputs("\\n", out);
			break;
		case '\r':
			fputs("\\r", out);
			break;
		case '\t':
			fputs("\\t", out);
			break;
		default:
			fputc(*text, out);
			break;
		}
	}
	fputc('"', out);
}

static char *document(const fixture_t *entries, size_t count) {
	char *body = NULL;
	size_t size = 0;
	FILE *out = open_memstream(&body, &size);
	assert(out);
	fputc('[', out);
	for (size_t i = 0; i < count; i++) {
		const fixture_t *entry = &entries[i];
		fprintf(out, "%s{\"draft\":%s,\"prerelease\":%s,\"tag_name\":", i ? "," : "", entry->draft ? "true" : "false", entry->prerelease ? "true" : "false");
		string(out, entry->tag);
		fputs(",\"name\":", out);
		string(out, entry->name);
		fputs(",\"body\":", out);
		string(out, entry->body);
		fputs(",\"assets\":[{\"name\":", out);
		string(out, entry->asset);
		fputs(",\"state\":", out);
		string(out, entry->asset_state);
		fprintf(out, ",\"size\":%ld,\"browser_download_url\":", entry->size);
		string(out, entry->url);
		fputs(",\"digest\":", out);
		string(out, entry->digest);
		fputs("}]}", out);
	}
	fputc(']', out);
	assert(fclose(out) == 0);
	return body;
}

static ota_result_t select_entries(const char *installed, const fixture_t *entries, size_t count) {
	char *body = document(entries, count);
	json_doc_t doc;
	assert(json_parse(body, &doc));
	installed_version = installed;
	snprintf(stem, sizeof(stem), "r1");
	release_valid = false;
	ota_result_t picked = pick_release(&doc);
	if (picked != OTA_OK) {
		assert(!release_valid);
	}
	json_free(&doc);
	free(body);
	checks++;
	return picked;
}

static void expect_fixture(fixture_t entry, ota_result_t expected) { assert(select_entries("1.3.0-ui3-alpha25", &entry, 1) == expected); }

static bool version_object(const char *body, ota_version_t *out) {
	json_doc_t doc;
	assert(json_parse(body, &doc));
	bool valid = release_version(&doc, json_root(&doc), out);
	json_free(&doc);
	checks++;
	return valid;
}

int main(void) {
	compare("1.3.0-ui3-alpha25", "1.3.0-ui3-alpha24", 1);
	compare("1.3.0-ui3-alpha10", "1.3.0-ui3-alpha9", 1);
	compare("1.3.0-ui3-beta1", "1.3.0-ui3-alpha99", 1);
	compare("1.3.0-ui3-rc1", "1.3.0-ui3-beta99", 1);
	compare("1.3.0-ui3", "1.3.0-ui3-rc99", 1);
	compare("1.3.0-ui3-stable", "1.3.0-ui3", 0);
	compare("1.3.0", "1.3.0-ui3-alpha25", -1);
	compare("1.3.0-ui2", "1.3.0-ui3-alpha1", -1);
	compare("1.3.1", "1.3.0-ui3-alpha25", 1);
	compare("V1.0.2-r1", "1.0.2", 0);
	compare("v1.2", "1.2.0.0", 0);
	compare("1.3.0-ui3-alpha.25", "1.3.0-ui3-alpha25", 0);
	const char *invalid[] = {"", "r2", "release", " 1.3", "1.3.", "1.2.3.4.5", "1.3.0-ui", "1.3.0-ui3-alpha", "1.3.0-ui3-alpha25garbage", "1.3.0-garbage", "1.3.0-ui3-alpha-1", "1.3.0-ui3-alpha25 extra", "999999999999999999999999999.1", "2147483648.0", "1.3.0-ui2147483648"};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
		invalid_version(invalid[i]);
	}

	expect_fixture(standard, OTA_OK);
	assert(release_valid && strcmp(release.tag, "r2") == 0 && release.size == 49780736);
	assert(strcmp(release.sha256, standard.digest + 7) == 0 && strcmp(release.url, standard.url) == 0);

	fixture_t entry = standard;
	entry.body = "## Notes\r\n\r\n  <!-- Sonix-Version: 1.3.0-ui3-alpha26 -->  \r\nOther notes";
	expect_fixture(entry, OTA_OK);
	entry.body = "Sonix-Version: 1.3.0-ui3-alpha25";
	expect_fixture(entry, OTA_UP_TO_DATE);
	entry.body = "Sonix-Version: 1.3.0-ui3-alpha24";
	expect_fixture(entry, OTA_UP_TO_DATE);
	entry.body = "Sonix-Version: 1.3.0";
	expect_fixture(entry, OTA_UP_TO_DATE);
	entry.body = "Sonix-Version: 1.3.1";
	expect_fixture(entry, OTA_OK);

	// A stable channel never silently opts into a prerelease, even if its flag is false.
	entry = standard;
	assert(select_entries("1.3.0-ui3", &entry, 1) == OTA_UP_TO_DATE);
	entry.prerelease = true;
	assert(select_entries("1.3.0-ui3-alpha25", &entry, 1) == OTA_OK);
	assert(select_entries("1.3.0-ui3", &entry, 1) == OTA_UP_TO_DATE);
	entry.draft = true;
	expect_fixture(entry, OTA_UP_TO_DATE);

	// Published snapshots: editorial titles are not comparable OS_version values.
	fixture_t real[2] = {standard, standard};
	real[0].body = "";
	real[1].tag = "release";
	real[1].name = "v1.1";
	real[1].body = "";
	real[1].size = 49422336;
	real[1].digest = "sha256:783a03f3d7992d9cf546947b6e022b6cf2ee596be9412ef465553c4f5db28aa1";
	assert(select_entries("1.3.0-ui3-alpha25", real, 2) == OTA_CHECK_FAILED);
	real[0].body = "<!-- Sonix-Version: 1.3.0-ui3-alpha25 -->";
	real[1].body = "<!-- Sonix-Version: 1.3.0-ui3-alpha20 -->";
	assert(select_entries("1.3.0-ui3-alpha25", real, 2) == OTA_UP_TO_DATE);
	assert(select_entries("1.3.0-ui3-alpha24", real, 2) == OTA_OK);
	assert(strcmp(release.tag, "r2") == 0);

	entry = standard;
	entry.tag = "v1.3.1";
	entry.body = NULL;
	expect_fixture(entry, OTA_OK);
	entry.tag = "v1.3.0-ui3-alpha26";
	expect_fixture(entry, OTA_OK);
	entry.tag = "v1.3.0-ui3-alpha24";
	expect_fixture(entry, OTA_UP_TO_DATE);
	entry.tag = "v1.3.0-ui3-alpha260000000000000000000000000000000000000000000";
	expect_fixture(entry, OTA_CHECK_FAILED);
	entry.tag = "v1.3.1";
	entry.body = "Sonix-Version: nonsense";
	expect_fixture(entry, OTA_CHECK_FAILED);
	entry.body = "Sonix-Version: 1.3.0-ui3-alpha26\nSonix-Version: 1.3.0-ui3-alpha27";
	expect_fixture(entry, OTA_CHECK_FAILED);
	entry.body = "Sonix-Version: 1.3.0-ui3-alpha26\nSonix-Version: 1.3.0-ui3-alpha26";
	expect_fixture(entry, OTA_CHECK_FAILED);
	entry.body = "<!-- Sonix-Version: 1.3.0-ui3-alpha26";
	expect_fixture(entry, OTA_CHECK_FAILED);
	entry.body = "<!-- Sonix-Version: 1.3.0-ui3-alpha26 --> extra";
	expect_fixture(entry, OTA_CHECK_FAILED);
	entry.body = "Sonix-Version: 1.3.0-ui3-alpha26 extra";
	expect_fixture(entry, OTA_CHECK_FAILED);
	// The complete body is metadata input; OTA_NOTES_MAX is only a UI preview.
	char long_body[OTA_NOTES_MAX + 256];
	memset(long_body, 'a', sizeof(long_body));
	const char *marker = "Sonix-Version: 1.3.0-ui3-alpha26\n";
	snprintf(long_body + OTA_NOTES_MAX + 1, 255, "\n[spoiler] \\\"notes\\\" \\ \\u0410\n<!-- Sonix-Version: 1.3.0-ui3-alpha26 -->\n");
	entry = standard;
	entry.body = long_body;
	expect_fixture(entry, OTA_OK);
	assert(strlen(release.notes) == OTA_NOTES_MAX - 1);
	memcpy(long_body, marker, strlen(marker));
	expect_fixture(entry, OTA_CHECK_FAILED); // Duplicate footer was beyond the preview.
	snprintf(long_body + OTA_NOTES_MAX + 1, 255, "\nSonix-Version: 1.3.0-ui3-alpha27\n");
	expect_fixture(entry, OTA_CHECK_FAILED); // Conflicting footer.
	memset(long_body, 'a', OTA_NOTES_MAX + 1);
	snprintf(long_body + OTA_NOTES_MAX + 1, 255, "\n<!-- Sonix-Version: 1.3.0-ui3-alpha26\n");
	expect_fixture(entry, OTA_CHECK_FAILED);
	long_body[OTA_NOTES_MAX + 1] = '\0';
	entry.tag = "v1.3.1";
	expect_fixture(entry, OTA_OK); // Long notes without a marker still allow numeric tags.

	ota_version_t from_object, expected;
	assert(parse_version("1.3.0-ui3-alpha26", &expected));
	assert(version_object("{\"tag_name\":\"r2\",\"body\":\"\\u0410\\u0431 \\\"notes\\\" \\\\ \\n<!-- Sonix-Version: 1.3.0-ui3-alpha26 -->\\r\\n\"}", &from_object));
	assert(version_cmp(&from_object, &expected) == 0);
	assert(version_object("{\"tag_name\":\"v1.3.1\"}", &from_object));
	assert(version_object("{\"tag_name\":\"v1.3.1\",\"body\":null}", &from_object));
	assert(!version_object("{\"tag_name\":\"v1.3.1\",\"body\":123}", &from_object));

	char *over_budget = malloc(LIST_LIMIT + 2);
	assert(over_budget);
	memset(over_budget, 'a', LIST_LIMIT + 1);
	over_budget[LIST_LIMIT + 1] = '\0';
	entry = standard;
	entry.body = over_budget;
	int before_alloc = body_allocations;
	expect_fixture(entry, OTA_CHECK_FAILED);
	assert(body_allocations == before_alloc); // Bound is checked before allocation.
	free(over_budget);

	entry = standard;
	before_alloc = body_allocations;
	fail_body_alloc = true;
	expect_fixture(entry, OTA_CHECK_FAILED);
	fail_body_alloc = false;
	assert(body_allocations == before_alloc + 1);
	expect_fixture(entry, OTA_OK); // OOM did not poison a later check.

	entry = standard;
	entry.asset = "r3proii.upt";
	expect_fixture(entry, OTA_UP_TO_DATE);
	entry = standard;
	entry.asset_state = "new";
	expect_fixture(entry, OTA_UP_TO_DATE);
	entry = standard;
	entry.asset = "R1.UPT";
	expect_fixture(entry, OTA_OK);
	entry = standard;
	entry.size = 0;
	expect_fixture(entry, OTA_CHECK_FAILED);
	entry = standard;
	entry.url = "http://example.invalid/r1.upt";
	expect_fixture(entry, OTA_CHECK_FAILED);
	entry = standard;
	entry.digest = "sha256:bad";
	expect_fixture(entry, OTA_CHECK_FAILED);
	entry = standard;
	entry.digest = "sha256:zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz";
	expect_fixture(entry, OTA_CHECK_FAILED);
	entry = standard;
	entry.digest = NULL;
	expect_fixture(entry, OTA_OK);
	assert(!release.sha256[0]);
	assert(select_entries("unknown", &standard, 1) == OTA_CHECK_FAILED);
	entry = standard;
	entry.tag = "";
	expect_fixture(entry, OTA_CHECK_FAILED);

	// Array order and marketing version must not override the actual complete version.
	fixture_t ordered[2] = {standard, standard};
	ordered[0].tag = "r99";
	ordered[0].name = "v9999";
	ordered[0].body = "Sonix-Version: 1.3.0-ui3-alpha26";
	ordered[1].tag = "r3";
	ordered[1].name = "v1.3";
	ordered[1].body = "Sonix-Version: 1.3.0-ui3-alpha27";
	assert(select_entries("1.3.0-ui3-alpha25", ordered, 2) == OTA_OK);
	assert(strcmp(release.tag, "r3") == 0);
	fixture_t swapped[2] = {ordered[1], ordered[0]};
	assert(select_entries("1.3.0-ui3-alpha25", swapped, 2) == OTA_OK);
	assert(strcmp(release.tag, "r3") == 0);

	// Bootstrap alias: the actual old four-part parser sees .26 above .0.
	// The new marker-aware parser then sees alpha26 as already installed.
	long legacy_installed[VERSION_PARTS], legacy_alias[VERSION_PARTS];
	assert(legacy_parse_version("1.3.0-ui3-alpha25", legacy_installed));
	assert(legacy_parse_version("v1.3.0.26", legacy_alias));
	assert(legacy_installed[3] == 0 && legacy_alias[3] == 26);
	assert(legacy_version_cmp(legacy_alias, legacy_installed) > 0);
	checks++;
	assert(legacy_parse_version("1.3.0-ui3-alpha26", legacy_installed));
	assert(legacy_version_cmp(legacy_alias, legacy_installed) > 0);
	checks++; // Only the installed NEW client prevents a repeated alias offer.
	assert(!legacy_parse_version("r2", legacy_alias));
	checks++;

	fixture_t bridge = standard;
	bridge.tag = "v1.3.0.26";
	bridge.body = "<!-- Sonix-Version: 1.3.0-ui3-alpha26 -->";
	bridge.url = "https://github.com/Rekkod-Tree/sonix-player-rekkod-mod/releases/download/v1.3.0.26/r1.upt";
	assert(!bridge.draft && !bridge.prerelease && !strcmp(bridge.asset, "r1.upt") && !strcmp(bridge.asset_state, "uploaded"));
	assert(select_entries("1.3.0-ui3-alpha25", &bridge, 1) == OTA_OK);
	assert(!strcmp(release.tag, bridge.tag));
	assert(select_entries("1.3.0-ui3-alpha26", &bridge, 1) == OTA_UP_TO_DATE);
	fixture_t bridge_and_history[3] = {bridge, real[0], real[1]};
	assert(select_entries("1.3.0-ui3-alpha25", bridge_and_history, 3) == OTA_OK);
	assert(!strcmp(release.tag, bridge.tag));
	assert(select_entries("1.3.0-ui3-alpha26", bridge_and_history, 3) == OTA_UP_TO_DATE);
	bridge.body = NULL;
	assert(select_entries("1.3.0-ui3-alpha26", &bridge, 1) == OTA_OK);
	// Omitting the actual-version marker would offer the alias again.

	// No retry storm or /latest fallback on offline, rate-limit, server or JSON errors.
	snprintf(repo, sizeof(repo), "Rekkod-Tree/sonix-player-rekkod-mod");
	snprintf(stem, sizeof(stem), "r1");
	installed_version = "1.3.0-ui3-alpha25";
	wifi_state = WIFI_STATE_OFF;
	fetch_count = 0;
	assert(do_check() == OTA_NO_NETWORK && fetch_count == 0);
	checks++;
	wifi_state = WIFI_STATE_CONNECTED;
	stem[0] = 0;
	assert(do_check() == OTA_NO_MODEL && fetch_count == 0);
	checks++;
	snprintf(stem, sizeof(stem), "r1");
	fetch_ok = false;
	for (int i = 0; i < 2; i++) {
		fetch_status = i == 0 ? 403 : 503;
		fetch_count = 0;
		assert(do_check() == OTA_CHECK_FAILED && fetch_count == 1);
		checks++;
	}
	fetch_ok = true;
	fetch_status = 200;
	fetch_body = "not JSON";
	fetch_count = 0;
	assert(do_check() == OTA_CHECK_FAILED && fetch_count == 1);
	checks++;
	fetch_body = "{\"message\":\"API rate limit exceeded\"}";
	assert(do_check() == OTA_CHECK_FAILED);
	checks++;
	fetch_body = "[]";
	cancel_requested = true;
	assert(do_check() == OTA_CANCELLED);
	checks++;
	cancel_requested = false;
	puts("OTA release/version fixtures passed; no network or device operations.");
	printf("Checks: %d\n", checks);
	return 0;
}
