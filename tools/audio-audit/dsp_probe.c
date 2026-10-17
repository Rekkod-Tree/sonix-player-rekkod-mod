// Numerical probe of the production DSP, with no audio or device output.
#include <assert.h>
#include <limits.h>
#include "src/system/audio/eq.c"

int main(int argc, char **argv) {
	if (argc == 2) {
		peq_band_t parsed[PEQ_BANDS];
		int preamp = 0, ignored = 0;
		assert(peq_read_apo(argv[1], parsed, &preamp, &ignored));
		assert(preamp == -20 && ignored == 0);
		assert(parsed[0].on && parsed[0].freq == 2200 && parsed[0].gain_tenths == 15 && parsed[0].q_cent == 90);
		assert(parsed[1].on && parsed[1].freq == 250 && parsed[1].gain_tenths == 8 && parsed[1].q_cent == 70);
		puts("PRESET_IMPORT_PASS: production APO parser, exact preamp/frequencies/gains/Q");
		return 0;
	}
	sf_enabled = 1;
	const int32_t sample = 1717986917; // 0.8 full scale
	for (int width = 100; width <= 200; width += 100) {
		int32_t frames[] = {sample, -sample, sample, sample};
		sf_width = width;
		soundfield_process_s32(frames, 2, 2);
		printf("WIDTH %d antiphase %.6f %.6f mono %.6f %.6f\n", width,
			frames[0] / 2147483648.0, frames[1] / 2147483648.0,
			frames[2] / 2147483648.0, frames[3] / 2147483648.0);
		assert(frames[2] == sample && frames[3] == sample);
		if (width == 100) assert(frames[0] == sample && frames[1] == -sample);
		else assert(frames[0] == INT32_MAX && frames[1] == INT32_MIN);
	}
	int values[MSEB_BANDS] = {-23, 8, 1, 0, 17, 0, 22, 19, 0, 29};
	for (int i = 0; i < MSEB_BANDS; i++) mseb_value[i] = values[i];
	mseb_enabled = 1;
	biquad_t bank[MSEB_FILTERS];
	int count = mseb_bank_build(bank, 44100);
	double pre = mseb_auto_preamp_db(44100), peak = -1000, peak_hz = 0;
	printf("MSEB_PREAMP %.4f dB\n", pre);
	for (int n = 0; n <= 1000; n++) {
		double hz = 20 * pow(1000.0, n / 1000.0), db = pre;
		for (int i = 0; i < count; i++) db += biquad_gain_db_at(&bank[i], 2 * M_PI * hz / 44100);
		if (db > peak) { peak = db; peak_hz = hz; }
		printf("CURVE %.4f %.4f\n", hz, db);
	}
	printf("MSEB_PEAK %.4f dB at %.2f Hz\n", peak, peak_hz);
	// Two overlapping PEQ bands expose the difference between maximum band
	// gain and maximum cascade gain. This is not the user's active profile.
	mseb_enabled = 0;
	peq_enabled = 1;
	peq_preamp_tenths = 0;
	for (int i = 0; i < PEQ_BANDS; i++) peq_bands[i].on = false;
	for (int i = 0; i < 2; i++)
		peq_bands[i] = (peq_band_t){.on=true, .type=PEQ_TYPE_PEAK, .freq=1000, .gain_tenths=60, .q_cent=100};
	filters_rebuild(44100);
	double cascade = 0;
	for (int i = 0; i < CHAIN_SLOTS; i++)
		if (chain_active[i]) cascade += biquad_gain_db_at(&chain[i], 2 * M_PI * 1000 / 44100);
	printf("OVERLAP_PEAK %.4f dB preamp %.4f dB net %.4f dB\n", cascade,
		20 * log10(pregain_target), cascade + 20 * log10(pregain_target));
	return 0;
}
