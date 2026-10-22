// Test-only compatibility reference from eq.c 86163c17; never linked into firmware.
static bool reference_read_apo(const char *path, peq_band_t *bands, int *preamp_tenths, int *ignored) {
	FILE *f = fopen(path, "r");
	if (!f) {
		return false;
	}
	for (int i = 0; i < PEQ_BANDS; i++) {
		bands[i] = peq_default_band(i);
	}
	*preamp_tenths = 0;
	*ignored = 0;

	int count = 0;
	char line[256];
	bool first = true;
	while (fgets(line, sizeof(line), f)) {
		char *p = line;
		// A file saved by Notepad starts with a UTF-8 byte order mark, and
		// "\xEF\xBB\xBFPreamp" is not "Preamp": the preamp line was skipped.
		if (first && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) {
			p += 3;
		}
		first = false;
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		if (*p == '#' || *p == '\0') {
			continue;
		}
		char *colon = strchr(p, ':');
		if (!colon) {
			continue;
		}
		*colon = '\0';
		char *rest = colon + 1;

		if (strcasecmp(p, "Preamp") == 0) {
			double db;
			char *cursor = rest;
			if (word_number(next_word(&cursor), &db)) {
				*preamp_tenths = clamp_int(round_scaled(db, 10.0), PEQ_PREAMP_MIN_TENTHS, PEQ_PREAMP_MAX_TENTHS);
			}
		} else if (strncasecmp(p, "Channel", 7) == 0) {
			// A second curve for one of the channels. This chain has one curve
			// for both, so the first is the one that is used.
			if (count > 0) {
				break;
			}
		} else if (strncasecmp(p, "Filter", 6) == 0) {
			peq_band_t b;
			apo_filter_t parsed = apo_parse_filter(rest, &b);
			if (parsed == APO_FILTER_EMPTY) {
				continue;
			}
			if (parsed == APO_FILTER_UNUSABLE) {
				(*ignored)++;
			} else if (count < PEQ_BANDS) {
				bands[count++] = b;
			} else {
				(*ignored)++;
			}
		}
	}
	fclose(f);
	return count > 0;
}
