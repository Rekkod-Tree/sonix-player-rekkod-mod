// Link this test against the STOCK target libasound, then run it with the
// candidate in LD_LIBRARY_PATH. Stock R1 has unversioned ALSA symbols: linking
// only against the candidate can hide resolution to obsolete compatibility ABIs.
// cc -std=gnu11 -O2 tests/alsa_abi_test.c -lasound -o /tmp/alsa_abi_test
#include <alsa/asoundlib.h>
#include <assert.h>
#include <stdio.h>

int main(void) {
	snd_pcm_t *pcm;
	assert(snd_pcm_open(&pcm, "null", SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK) == 0);
	assert(snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, 48000, 1, 100000) == 0);
	snd_pcm_hw_params_t *params;
	snd_pcm_hw_params_alloca(&params);
	assert(snd_pcm_hw_params_current(pcm, params) == 0);
	snd_pcm_uframes_t period = 0, buffer = 0;
	int period_rc = snd_pcm_hw_params_get_period_size(params, &period, NULL);
	int buffer_rc = snd_pcm_hw_params_get_buffer_size(params, &buffer);
	printf("ALSA %s: period_rc=%d period=%lu buffer_rc=%d buffer=%lu\n", snd_asoundlib_version(), period_rc, (unsigned long)period, buffer_rc, (unsigned long)buffer);
	fflush(stdout);
	assert(period_rc == 0 && buffer_rc == 0 && period > 0 && buffer >= period);
	short samples[1024] = {0};
	assert(snd_pcm_writei(pcm, samples, 512) == 512);
	assert(snd_pcm_drop(pcm) == 0 && snd_pcm_prepare(pcm) == 0);
	assert(snd_pcm_writei(pcm, samples, 512) == 512);
	snd_pcm_close(pcm);
	puts("PASS stock-linked ALSA PCM getter ABI and output lifecycle");
	return 0;
}
