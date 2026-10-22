// Same public-API PCM trace for baseline and candidate; no real config/card.
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/system/core/config.h"
#ifdef BASELINE_CONTROL
static pthread_barrier_t entered,proceed;
static void fixture_slot(int slot);
#endif
#include "actual_eq.c"
long config_get_int(const char *s,const char *k,long fallback) {(void)s;(void)k;return fallback;}
const char *config_get_copy(const char *s,const char *k,const char *fallback,char *out,size_t n) {(void)s;(void)k;if(!fallback)return NULL;snprintf(out,n,"%s",fallback);return out;}
void config_set_int(const char *s,const char *k,long value) {(void)s;(void)k;(void)value;}
void config_set(const char *s,const char *k,const char *v) {(void)s;(void)k;(void)v;}
bool config_save(void) {return true;}
static peq_preset_data_t preset(int kind) {peq_preset_data_t p={.preamp_tenths=-62};for(int i=0;i<PEQ_BANDS;i++){p.bands[i]=peq_default_band(i);p.bands[i].on=true;p.bands[i].type=i%PEQ_TYPE_COUNT;p.bands[i].freq=kind?1000+i*1300:200+i*1500;p.bands[i].gain_tenths=kind?-30:40;p.bands[i].q_cent=70+i*4;}return p;}
#ifdef BASELINE_CONTROL
static void fixture_slot(int slot) {if(slot==PEQ_SLOT0){pthread_barrier_wait(&entered);pthread_barrier_wait(&proceed);}}
static void *audio(void *unused) {(void)unused;filters_rebuild(48000);return NULL;}
int main(void) {
 eq_init();peq_preset_data_t a=preset(0),b=preset(1);assert(peq_preset_apply_parsed("A",&a,true));unsigned old=settings_version;
 assert(!pthread_barrier_init(&entered,NULL,2)&&!pthread_barrier_init(&proceed,NULL,2));pthread_t thread;assert(!pthread_create(&thread,NULL,audio,NULL));pthread_barrier_wait(&entered);assert(peq_preset_apply_parsed("B",&b,true));pthread_barrier_wait(&proceed);assert(!pthread_join(thread,NULL));
 biquad_t expected={0};assert(biquad_set_coeffs(&expected,peq_filter_type(b.bands[0].type),b.bands[0].freq,(double)b.bands[0].gain_tenths/10,(double)b.bands[0].q_cent/100,48000));assert(settings_version>old);
 assert(!(built_version==settings_version&&chain[PEQ_SLOT0].b0!=expected.b0));return 0;
}
#else
int main(int argc,char **argv) {
 assert(argc==5);int kind=atoi(argv[1]),rate=atoi(argv[2]),channels=atoi(argv[3]),bits=atoi(argv[4]);assert(rate>0&&(channels==1||channels==2)&&(bits==16||bits==32));eq_init();
 if(kind==1||kind==4){eq_set_enabled(true);for(int i=0;i<EQ_BANDS;i++)eq_adjust_band(i,(i%5)-2);}
 if(kind==2||kind==4){peq_preset_data_t p=preset(0);assert(peq_preset_apply_parsed("PCM",&p,true));}
 if(kind==3||kind==4){mseb_set_enabled(true);for(int i=0;i<MSEB_BANDS;i++)mseb_set_value(i,(i%2?-1:1)*(3+i));}
 uint32_t random=0x12834567u;short small[1024];int32_t wide[1024];
 for(int block=0;block<4;block++) {
  if(block==2&&kind){eq_adjust_band(1,-5);peq_set_preamp(-35);}
  for(int i=0;i<512*channels;i++){random^=random<<13;random^=random>>17;random^=random<<5;int value=(int)(random&0x3fff)-8192;small[i]=(short)value;wide[i]=(int32_t)((int64_t)value*65536+(int)(random&0xffff)-32768);}
  if(bits==16){eq_process(small,512,channels,rate);assert(fwrite(small,sizeof(*small),(size_t)512*channels,stdout)==(size_t)512*channels);}
  else {eq_process_s32(wide,512,channels,rate);assert(fwrite(wide,sizeof(*wide),(size_t)512*channels,stdout)==(size_t)512*channels);}
 }
 return 0;
}
#endif
