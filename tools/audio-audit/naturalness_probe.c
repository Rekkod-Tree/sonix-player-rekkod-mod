// Verify the three comparison presets through the production parser and DSP.
#include <assert.h>
#include "src/system/audio/eq.c"

int main(int argc, char **argv) {
	assert(argc == 4);
	int values[MSEB_BANDS] = {-23, 8, 1, 0, 17, 0, 22, 19, 0, 29};
	for (int i = 0; i < MSEB_BANDS; i++) mseb_value[i] = values[i];
	mseb_enabled = 1;
	peq_enabled = 1;
	const int frequencies[] = {100, 250, 1000, 2200, 8000, 12000, 16000};
	for (int variant = 0; variant < 3; variant++) {
		int preamp = 0, ignored = 0;
		assert(peq_read_apo(argv[variant + 1], peq_bands, &preamp, &ignored));
		assert(preamp == -20 && ignored == 0);
		assert(peq_bands[0].on && peq_bands[0].freq == 2200 && peq_bands[0].gain_tenths == 15 && peq_bands[0].q_cent == 90);
		assert(peq_bands[1].on && peq_bands[1].freq == 250 && peq_bands[1].gain_tenths == (variant == 1 ? 12 : 8) && peq_bands[1].q_cent == 70);
		if (variant == 2)
			assert(peq_bands[2].on && peq_bands[2].type == PEQ_TYPE_HIGHSHELF && peq_bands[2].freq == 8000 && peq_bands[2].gain_tenths == -15 && peq_bands[2].q_cent == 70);
		else assert(!peq_bands[2].on);
		peq_preamp_tenths = preamp;
		filters_rebuild(44100);
		printf("VARIANT %d preamp %.3f\n", variant, 20 * log10(pregain_target));
		for (size_t n = 0; n < sizeof(frequencies) / sizeof(frequencies[0]); n++) {
			double db = mseb_pregain_db + 20 * log10(pregain_target);
			for (int i = 0; i < CHAIN_SLOTS; i++)
				if (chain_active[i]) db += biquad_gain_db_at(&chain[i], 2 * M_PI * frequencies[n] / 44100);
			printf("RESPONSE %d %d %.5f\n", variant, frequencies[n], db);
		}
	}
	puts("PASS: exact frequencies/gains/Q, no dropped filters, constant preamp; current MSEB retained");
	return 0;
}
