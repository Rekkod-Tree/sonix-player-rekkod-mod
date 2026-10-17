#include <assert.h>
#include "src/system/audio/eq.c"
int main(int argc, char **argv) {
    assert(argc == 2);
    int preamp = 0, ignored = 0;
    assert(peq_read_apo(argv[1], peq_bands, &preamp, &ignored));
    assert(preamp == -20 && ignored == 0);
    assert(peq_bands[0].on && peq_bands[0].freq == 2200 && peq_bands[0].gain_tenths == 15 && peq_bands[0].q_cent == 90);
    assert(peq_bands[1].on && peq_bands[1].freq == 250 && peq_bands[1].gain_tenths == 12 && peq_bands[1].q_cent == 70);
    assert(peq_bands[2].on && peq_bands[2].freq == 3500 && peq_bands[2].gain_tenths == 7 && peq_bands[2].q_cent == 90);
    assert(peq_bands[3].on && peq_bands[3].type == PEQ_TYPE_HIGHSHELF && peq_bands[3].freq == 8000 && peq_bands[3].gain_tenths == -10 && peq_bands[3].q_cent == 70);
    for (int i = 4; i < PEQ_BANDS; i++) assert(!peq_bands[i].on);
    peq_enabled = 1;
    peq_preamp_tenths = preamp;
    filters_rebuild(44100);
    double peak = -100, peak_hz = 0;
    for (int f = 20; f <= 20000; f++) {
        double db = 20 * log10(pregain_target);
        for (int i = 0; i < CHAIN_SLOTS; i++)
            if (chain_active[i]) db += biquad_gain_db_at(&chain[i], 2 * M_PI * f / 44100);
        if (db > peak) { peak = db; peak_hz = f; }
    }
    printf("PEQ-only peak: %.4f dB at %.0f Hz; preamp -2 dB.\n", peak, peak_hz);
    assert(peak <= 0.0);
    puts("PASS: exact preset parsed, no dropped bands, PEQ-only steady-state response below unity. MSEB and headphone response are outside this check.");
}