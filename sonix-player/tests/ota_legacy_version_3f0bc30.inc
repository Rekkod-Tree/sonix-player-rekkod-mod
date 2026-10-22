// Unchanged version block from src/system/device/ota.c at
// 3f0bc30e4708e918d469e6ade9ec1c1abba92282. Test-only macro aliases rename it.
#define VERSION_PARTS 4

// "1.0.2", "v1.0.2", "1.0.2-r1": up to four numbers, a leading v ignored,
// anything after the numbers ignored. False when there is no number at all.
static bool parse_version(const char *text, long out[VERSION_PARTS]) {
	memset(out, 0, sizeof(long) * VERSION_PARTS);
	const char *p = text;
	if (*p == 'v' || *p == 'V') {
		p++;
	}
	if (!isdigit((unsigned char)*p)) {
		return false;
	}
	for (int i = 0; i < VERSION_PARTS && isdigit((unsigned char)*p); i++) {
		out[i] = strtol(p, (char **)&p, 10);
		if (*p != '.') {
			break;
		}
		p++;
	}
	return true;
}

static int version_cmp(const long a[VERSION_PARTS], const long b[VERSION_PARTS]) {
	for (int i = 0; i < VERSION_PARTS; i++) {
		if (a[i] != b[i]) {
			return a[i] < b[i] ? -1 : 1;
		}
	}
	return 0;
}
