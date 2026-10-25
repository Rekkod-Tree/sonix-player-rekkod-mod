// Cached negotiated format: no ALSA device, filesystem, worker or timer.
#define _GNU_SOURCE
#include "src/system/audio/audio.c"
#include <assert.h>

int main(void) {
	audio_output_format_t info;
	assert(!audio_get_output_format(NULL));
	pcm_device_open = false;
	output_format = (audio_output_format_t){96000, 24, 2, false};
	strcpy(output_format_device, "default");
	strcpy(output_pcm, "default");
	assert(!audio_get_output_format(&info) && info.bits == 0 && info.rate == 0);
	pcm_device_open = true;
	// Some opens already own audio_mutex; the UI getter must never acquire it.
	pthread_mutex_lock(&audio_mutex);
	assert(audio_get_output_format(&info) && info.bits == 24 && info.rate == 96000 && info.channels == 2);
	pthread_mutex_unlock(&audio_mutex);
	strcpy(output_pcm, "hw:1,0");
	assert(!audio_get_output_format(&info) && info.rate == 0);
	strcpy(output_format_device, "hw:1,0");
	output_format = (audio_output_format_t){48000, 16, 2, false};
	assert(audio_get_output_format(&info) && info.bits == 16 && info.rate == 48000);
	output_format.bits = 0;
	assert(!audio_get_output_format(&info));
	output_format = (audio_output_format_t){176400, 24, 2, true};
	assert(audio_get_output_format(&info) && info.dop);
	pcm_device_open = false;
	assert(!audio_get_output_format(&info) && !info.dop);
	puts("PASS negotiated format cache, no audio-lock recursion, changed route, unavailable and DoP output");
	return 0;
}
