// Actual module, real mutexes/barriers, and inert persistence; no device I/O.
#define _GNU_SOURCE
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/system/core/config.h"
static _Thread_local bool audio_role,settings_held;
static unsigned saves,publishes,weight_builds,audio_tries;
static bool pause_publish,pause_rebuild,pause_hint;
static pthread_barrier_t entered,proceed;
static int fixture_lock(pthread_mutex_t *mutex);
static int fixture_unlock(pthread_mutex_t *mutex);
static int fixture_trylock(pthread_mutex_t *mutex) __attribute__((unused));
static void fixture_published(void);
static void fixture_rebuilt_slot(int slot);
static void fixture_hint(void);
static void fixture_weights(void) {weight_builds++;}
#define pthread_mutex_lock fixture_lock
#define pthread_mutex_unlock fixture_unlock
#define pthread_mutex_trylock fixture_trylock
#include "actual_eq.c"
#undef pthread_mutex_lock
#undef pthread_mutex_unlock
#undef pthread_mutex_trylock
static int fixture_lock(pthread_mutex_t *mutex) {if(mutex==&settings_lock)assert(!audio_role);int r=pthread_mutex_lock(mutex);if(!r&&mutex==&settings_lock){assert(!settings_held);settings_held=true;}return r;}
static int fixture_unlock(pthread_mutex_t *mutex) {if(mutex==&settings_lock){assert(settings_held);settings_held=false;}return pthread_mutex_unlock(mutex);}
static int fixture_trylock(pthread_mutex_t *mutex) {if(audio_role&&mutex==&settings_lock)audio_tries++;int r=pthread_mutex_trylock(mutex);if(!r&&mutex==&settings_lock){assert(!settings_held);settings_held=true;}return r;}
static void barrier(void) {pthread_barrier_wait(&entered);pthread_barrier_wait(&proceed);}
static void fixture_published(void) {publishes++;if(pause_publish)barrier();}
static void fixture_rebuilt_slot(int slot) {if(pause_rebuild&&slot==PEQ_SLOT0)barrier();}
static void fixture_hint(void) {if(pause_hint&&audio_role)barrier();}
long config_get_int(const char *s,const char *k,long fallback) {(void)s;(void)k;return fallback;}
const char *config_get_copy(const char *s,const char *k,const char *fallback,char *out,size_t n) {(void)s;(void)k;if(!fallback)return NULL;snprintf(out,n,"%s",fallback);return out;}
void config_set_int(const char *s,const char *k,long value) {(void)s;(void)k;(void)value;assert(!settings_held);}
void config_set(const char *s,const char *k,const char *value) {(void)s;(void)k;(void)value;assert(!settings_held);}
bool config_save(void) {assert(!settings_held&&!audio_role);saves++;return true;}
static void same_band(const peq_band_t *a,const peq_band_t *b) {assert(a->on==b->on&&a->type==b->type&&a->freq==b->freq&&a->gain_tenths==b->gain_tenths&&a->q_cent==b->q_cent);}
static void published_matches_live(void) {
 assert(published_settings.version==atomic_load_explicit(&settings_version,memory_order_relaxed));
 assert(published_settings.eq_on==eq_get_enabled()&&published_settings.mseb_on==mseb_get_enabled()&&published_settings.peq_on==peq_get_enabled());
 for(int i=0;i<EQ_BANDS;i++)assert(published_settings.eq_gain[i]==band_gain_db[i]);
 for(int i=0;i<MSEB_BANDS;i++)assert(published_settings.mseb_value[i]==mseb_value[i]);
 for(int i=0;i<PEQ_BANDS;i++)same_band(&published_settings.peq_band[i],&peq_bands[i]);
 assert(published_settings.preamp_tenths==peq_preamp_tenths);
}
static peq_preset_data_t preset(int which) {
 peq_preset_data_t value={.preamp_tenths=which?-65:-20};
 for(int i=0;i<PEQ_BANDS;i++){value.bands[i]=peq_default_band(i);value.bands[i].on=true;value.bands[i].freq=which?500+i*1000:100+i*1200;value.bands[i].gain_tenths=which?-30:40;}
 return value;
}
static void set_a(void) {eq_set_enabled(true);eq_set_band(0,6);mseb_set_enabled(true);mseb_set_value(0,15);mseb_set_value(1,8);peq_preset_data_t value=preset(0);assert(peq_preset_apply_parsed("A",&value,true));}
static void set_b(void) {eq_set_band(0,-8);mseb_set_value(0,-13);mseb_set_value(1,-4);peq_preset_data_t value=preset(1);assert(peq_preset_apply_parsed("B",&value,true));}
static bool prepare(int rate) {audio_role=true;bool r=process_prepare(128,2,rate);audio_role=false;return r;}
static void barriers_init(void) {assert(!pthread_barrier_init(&entered,NULL,2)&&!pthread_barrier_init(&proceed,NULL,2));}
static void barriers_end(void) {assert(!pthread_barrier_destroy(&entered)&&!pthread_barrier_destroy(&proceed));}
static void *publish_eq_on(void *unused) {(void)unused;eq_set_enabled(true);return NULL;}
static void *publish_change(void *unused) {(void)unused;eq_adjust_band(0,-7);return NULL;}
static void *prepare_thread(void *rate) {assert(prepare((int)(intptr_t)rate));return NULL;}
static void *prepare_may_off(void *rate) {(void)prepare((int)(intptr_t)rate);return NULL;}
static void writers(void) {
 unsigned n=publishes;eq_init();assert(publishes==n+1&&weight_builds==1);published_matches_live();
 eq_set_enabled(true);eq_adjust_band(0,99);assert(eq_get_band(0)==EQ_GAIN_MAX_DB);published_matches_live();
 mseb_set_enabled(true);mseb_set_value(0,999);assert(mseb_get_value(0)==mseb_get_range());published_matches_live();
 peq_set_enabled(true);peq_band_t band=peq_default_band(0);band.on=true;band.freq=30000;peq_set_band(0,&band);published_matches_live();
 peq_set_preamp(-999);assert(peq_get_preamp()==PEQ_PREAMP_MIN_TENTHS);published_matches_live();
 peq_preset_data_t value=preset(1);assert(peq_preset_apply_parsed("Body Voice",&value,true));published_matches_live();
 peq_reset_band(0);published_matches_live();n=publishes;unsigned saved=saves;peq_reset();assert(publishes==n+12&&saves==saved+11);published_matches_live();
 mseb_set_range(20);published_matches_live();mseb_reset();published_matches_live();
 n=publishes;eq_reset();assert(publishes==n+10);n=publishes;eq_apply_preset(&EQ_DEFAULT_PRESETS[1]);assert(publishes==n+10);published_matches_live();
 n=publishes;eq_adjust_band(-1,4);peq_reset_band(-1);peq_set_band(PEQ_BANDS,&band);mseb_set_value(-1,1);assert(!peq_preset_apply_parsed("bad/name",&value,true));assert(publishes==n);
 assert(weight_builds==1);puts("PASS 13 writer paths, clamps/refusals, compound counts, persistence outside lock, startup weights");
}
static void rebuild_coherent(void) {
 set_a();assert(prepare(48000));biquad_t expected[CHAIN_SLOTS];memcpy(expected,chain,sizeof(expected));bool active[CHAIN_SLOTS];memcpy(active,chain_active,sizeof(active));float gain=pregain_target,mseb=mseb_pregain_target;unsigned version=audio_settings.version;
 built_version=version-1;barriers_init();pause_rebuild=true;pthread_t thread;assert(!pthread_create(&thread,NULL,prepare_thread,(void *)(intptr_t)48000));pthread_barrier_wait(&entered);
 set_b();unsigned newer=atomic_load_explicit(&settings_version,memory_order_relaxed);assert(newer!=version);pthread_barrier_wait(&proceed);assert(!pthread_join(thread,NULL));pause_rebuild=false;barriers_end();
 assert(built_version==version&&audio_settings.version==version&&pregain_target==gain&&mseb_pregain_target==mseb);
 for(int i=0;i<CHAIN_SLOTS;i++)assert(chain_active[i]==active[i]&&chain[i].b0==expected[i].b0&&chain[i].b1==expected[i].b1&&chain[i].b2==expected[i].b2&&chain[i].a1==expected[i].a1&&chain[i].a2==expected[i].a2);
 assert(prepare(48000)&&audio_settings.version==newer&&built_version==newer);assert(weight_builds==1);puts("PASS rebuild uses A through every coefficient/headroom while B publishes; next buffer adopts B");
}
static void busy_and_rate(void) {
 eq_init();assert(!prepare(48000));unsigned old=audio_settings.version,tries=audio_tries;
 barriers_init();pause_publish=true;pthread_t producer,audio;assert(!pthread_create(&producer,NULL,publish_eq_on,NULL));pthread_barrier_wait(&entered);
 assert(!pthread_create(&audio,NULL,prepare_may_off,(void *)(intptr_t)48000));assert(!pthread_join(audio,NULL));assert(audio_settings.version==old&&!audio_settings.eq_on&&audio_tries==tries+1);
 pthread_barrier_wait(&proceed);assert(!pthread_join(producer,NULL));pause_publish=false;barriers_end();assert(prepare(48000)&&audio_settings.eq_on);
 old=audio_settings.version;assert(built_rate==48000);barriers_init();pause_publish=true;assert(!pthread_create(&producer,NULL,publish_change,NULL));pthread_barrier_wait(&entered);
 assert(!pthread_create(&audio,NULL,prepare_thread,(void *)(intptr_t)96000));assert(!pthread_join(audio,NULL));assert(audio_settings.version==old&&built_version==old&&built_rate==96000);
 pthread_barrier_wait(&proceed);assert(!pthread_join(producer,NULL));pause_publish=false;barriers_end();assert(prepare(96000)&&audio_settings.version!=old&&built_rate==96000);
 eq_set_enabled(false);assert(!prepare(96000));assert(audio_settings.version==atomic_load_explicit(&settings_version,memory_order_relaxed));
 puts("PASS busy publication never waits/reads new payload; all-off catches enable/disable; rate fallback rebuilds prior snapshot");
}
static void newer_than_hint_and_wrap(void) {
 set_a();assert(prepare(48000));eq_adjust_band(0,7);unsigned hint=atomic_load_explicit(&settings_version,memory_order_relaxed);
 barriers_init();pause_hint=true;pthread_t thread;assert(!pthread_create(&thread,NULL,prepare_thread,(void *)(intptr_t)48000));pthread_barrier_wait(&entered);
 eq_adjust_band(0,-4);unsigned newest=atomic_load_explicit(&settings_version,memory_order_relaxed);assert(newest!=hint);pthread_barrier_wait(&proceed);assert(!pthread_join(thread,NULL));pause_hint=false;barriers_end();assert(audio_settings.version==newest&&built_version==newest);
 for(int i=0;i<3;i++){eq_adjust_band(0,i-2);}assert(prepare(48000));assert(audio_settings.version==published_settings.version);
 assert(!pthread_mutex_lock(&settings_lock));published_settings.version=UINT_MAX;audio_settings.version=UINT_MAX;built_version=UINT_MAX;atomic_store_explicit(&settings_version,UINT_MAX,memory_order_relaxed);assert(!pthread_mutex_unlock(&settings_lock));
 eq_adjust_band(0,3);assert(published_settings.version==0&&prepare(48000)&&audio_settings.version==0&&built_version==0);
 unsigned tries=audio_tries;assert(prepare(48000)&&audio_tries==tries);audio_role=true;assert(!process_prepare(0,2,48000)&&!process_prepare(4,3,48000)&&!process_prepare(4,2,0));audio_role=false;assert(audio_tries==tries);
 puts("PASS copied-object version beats stale hint, latest coalescing, uint32 wrap and unchanged/invalid fast paths");
}
int main(void) {writers();rebuild_coherent();busy_and_rate();newer_than_hint_and_wrap();puts("PASS actual #62 protocol and baseline-compatible public writers");return 0;}
