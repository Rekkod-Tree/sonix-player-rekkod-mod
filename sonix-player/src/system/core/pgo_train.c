// Only the instrumented training executable contains this thread. Production
// code and main's control flow remain identical between generation and use.
#ifdef SONIX_PROFILE_GENERATE
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>

extern void __gcov_dump(void);
static sigset_t requests;

static void *capture_profile(void *unused) {
	(void)unused;
	bool captured = false;
	for (;;) {
		int signal_number;
		if (sigwait(&requests, &signal_number) != 0)
			continue;
		if (!captured) {
			__gcov_dump();
			captured = true;
			fprintf(stderr, "profile: RAM training profile saved\n");
		}
	}
	return NULL;
}

__attribute__((constructor)) static void prepare_profile_capture(void) {
	sigemptyset(&requests);
	// SIGUSR2 already belongs to perfstat; keep its diagnostics available.
	sigaddset(&requests, SIGUSR1);
	if (pthread_sigmask(SIG_BLOCK, &requests, NULL) != 0)
		return;
	pthread_attr_t attributes;
	if (pthread_attr_init(&attributes) != 0)
		return;
	pthread_attr_setstacksize(&attributes, 128u * 1024u);
	pthread_t thread;
	int rc = pthread_create(&thread, &attributes, capture_profile, NULL);
	pthread_attr_destroy(&attributes);
	if (!rc)
		pthread_detach(thread);
	else
		fprintf(stderr, "profile: capture thread unavailable (%d)\n", rc);
}
#endif
