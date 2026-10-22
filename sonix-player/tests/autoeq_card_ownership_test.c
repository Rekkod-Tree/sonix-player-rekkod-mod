// Actual AutoEQ filesystem phases + actual storage token and PEQ/config code.
#define _GNU_SOURCE
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <unistd.h>
#include "src/system/audio/eq.h"
#include "src/system/device/system.h"
#include "src/system/net/http.h"
#include "src/system/core/config.h"
static bool config_fail,parse_oom;
static bool fixture_config_save(void) {if(config_fail)return false;return config_save();}
static FILE *fixture_fmemopen(void *text,size_t size,const char *mode) {if(parse_oom){errno=ENOMEM;return NULL;}return fmemopen(text,size,mode);}
#define config_save fixture_config_save
#define fmemopen fixture_fmemopen
#include "actual_eq.c"
#undef config_save
#undef fmemopen
#include "reference_parser.inc"
static const char *tr(const char *key) {return key;}
static char card[1024],old_card[1024],internal[1024],host_home[1024];
#define AUTOEQ_INTERNAL_ROOT internal
static pthread_mutex_t card_lock=PTHREAD_MUTEX_INITIALIZER,job_lock=PTHREAD_MUTEX_INITIALIZER;
static uint64_t card_epoch;
static bool card_epoch_valid,card_epoch_initialized,card_attached=true,card_released,card_usb_claimed,sd_writable=true,sd_requires_mount,job_closing;
static char sd_root[1024];
static bool usb_storage_active(void) {return false;}
static bool is_mounted(const char *path) {(void)path;return true;}
static int fault,hit;
static const char *target="/PEQ";
static bool swap_during_write,swapped,network_ready=true,real_fs,home_case,ram_cache;
static int network_calls,stage_observations;
static void change_owner(void);
static void swap_owner(void) {assert(!rename(card,old_card));assert(!mkdir(card,0700));change_owner();swapped=true;}
static void fd_path(int fd,char *out,size_t size) {char p[64];snprintf(p,sizeof(p),"/proc/self/fd/%d",fd);ssize_t n=readlink(p,out,size-1);assert(n>=0);out[n]=0;}
static bool matching(int fd) {char p[2048];fd_path(fd,p,sizeof(p));return strstr(p,target)!=NULL;}
static ssize_t fixture_write(int fd,const void *data,size_t size) {
 if(swap_during_write&&!swapped&&matching(fd))swap_owner();
 if(!hit&&matching(fd)) {
  if(fault==1){hit++;return 0;}if(fault==2){hit++;errno=EIO;return -1;}
  if(fault==3){hit++;errno=EINTR;return -1;}if(fault==4){hit++;return write(fd,data,size>1?size/2:size);}
 }
 return write(fd,data,size);
}
static int fixture_fsync(int fd) {
 struct stat st;assert(!fstat(fd,&st));
 if(!hit&&matching(fd)&&((fault==5&&S_ISREG(st.st_mode))||(fault==6&&S_ISDIR(st.st_mode)))){hit++;errno=EIO;return -1;}
 return fsync(fd);
}
static int fixture_close(int fd) {
 assert(fcntl(fd,F_GETFD)>=0);struct stat st;assert(!fstat(fd,&st));bool fail=!hit&&matching(fd)&&(fault==7||(fault==13&&S_ISDIR(st.st_mode)));int result=close(fd);
 if(fail){hit++;errno=EIO;return -1;}return result;
}
static int fixture_renameat(int a,const char *x,int b,const char *y) {
 if(!hit&&(fault==8||fault==14)&&matching(a)){hit++;errno=EIO;return -1;}return renameat(a,x,b,y);
}
static int fixture_unlinkat(int fd,const char *name,int flags) {
 if(fault==14&&matching(fd)){hit++;errno=EACCES;return -1;}
 if(!hit&&fault==9&&matching(fd)){hit++;errno=EIO;return -1;}return unlinkat(fd,name,flags);
}
static int fixture_fstatfs(int fd,struct statfs *out) {stage_observations++;int result=fstatfs(fd,out);if(!result&&!real_fs)out->f_type=fault==10?0x01021994:fault==15?0x858458f6:0x24051905;if(!result&&ram_cache){char p[2048];fd_path(fd,p,sizeof(p));if(strstr(p,"/.cache"))out->f_type=0x01021994;}return result;}
static int fixture_fstatvfs(int fd,struct statvfs *out) {int result=fstatvfs(fd,out);if(!result&&fault==11)out->f_bavail=0;return result;}
static char *fixture_getenv(const char *name) {if(home_case&&!strcmp(name,"XDG_CACHE_HOME"))return NULL;if(home_case&&!strcmp(name,"HOME"))return host_home;return getenv(name);}
#define write fixture_write
#define fsync fixture_fsync
#define close fixture_close
#define renameat fixture_renameat
#define unlinkat fixture_unlinkat
#define fstatfs fixture_fstatfs
#define fstatvfs fixture_fstatvfs
#define getenv fixture_getenv
#include "actual_storage.inc"
#include "actual_io_types.inc"
static bool peqautoeq_network_ready(void) {return network_ready;}
#include "actual_io_functions.inc"
#undef write
#undef fsync
#undef close
#undef renameat
#undef unlinkat
#undef fstatfs
#undef fstatvfs
#undef getenv
static void change_owner(void) {storage_card_revoke_locked();card_epoch_initialized=true;card_attached=true;snprintf(sd_root,sizeof(sd_root),"%s",card);storage_card_publish_locked();}
static const char *valid_profile="Preamp: -6.2 dB\nFilter 1: ON PK Fc 105 Hz Gain 2.4 dB Q 0.70\n";
static const char *index_text="- [Model](./measure/Model) by Fixture\n";
static const char *response;
static size_t response_position,response_length;
static bool unknown_length;
static void no_card_fds(void) {
 DIR *dir=opendir("/proc/self/fd");assert(dir);struct dirent *entry;
 while((entry=readdir(dir))){int fd=atoi(entry->d_name);if(fd<3||fd==dirfd(dir))continue;char p[2048],link[80];snprintf(link,sizeof(link),"/proc/self/fd/%d",fd);ssize_t n=readlink(link,p,sizeof(p)-1);if(n<0)continue;p[n]=0;assert(strncmp(p,card,strlen(card))&&strncmp(p,old_card,strlen(old_card)));}
 assert(!closedir(dir));
}
bool http_stream_open(http_stream_t *stream,const char *url,int timeout) {(void)url;assert(timeout==35);no_card_fds();network_calls++;memset(stream,0,sizeof(*stream));response_position=0;response_length=strlen(response?response:index_text);stream->content_length=unknown_length?0:(long)response_length;return true;}
int http_stream_read(http_stream_t *stream,void *buffer,int count) {(void)stream;no_card_fds();size_t left=response_length-response_position;if((size_t)count>left)count=(int)left;memcpy(buffer,(response?response:index_text)+response_position,(size_t)count);response_position+=(size_t)count;return count;}
void http_stream_close(http_stream_t *stream) {(void)stream;no_card_fds();}
bool http_get(const char *url,char **out,size_t *size,size_t limit,int timeout) {(void)url;assert(limit==PROFILE_MAX&&timeout==30);no_card_fds();network_calls++;*out=strdup(response?response:valid_profile);assert(*out);*size=strlen(*out);return true;}
const char *http_last_error(void) {return "fixture HTTP error";}
char *http_url_encode(const char *in,char *out,size_t size) {snprintf(out,size,"%s",in);return out;}
static void put(const char *path,const char *text) {FILE *f=fopen(path,"w");assert(f&&fputs(text,f)>=0&&fclose(f)==0);}
static void path(char *out,size_t size,const char *parent,const char *name) {int n=snprintf(out,size,"%s/%s",parent,name);assert(n>0&&(size_t)n<size);}
static void assert_text(const char *file,const char *text) {FILE *f=fopen(file,"r");assert(f);char b[512];size_t n=fread(b,1,sizeof(b)-1,f);b[n]=0;assert(!fclose(f)&&!strcmp(b,text));}
static int fd_count(void) {DIR *d=opendir("/proc/self/fd");assert(d);int n=0;while(readdir(d))n++;assert(!closedir(d));return n;}
static job_t job(void) {job_t result={0};char root[1024];assert(storage_card_try_admit(&result.card,root,sizeof(root)));strcpy(result.preset,"Body Voice");strcpy(result.cache,"12345678.txt");strcpy(result.query,"Model");strcpy(result.selected.name,"Model");strcpy(result.selected.path,"./measure/Model");return result;}
static void profile_directories(char *profile,size_t size) {int p=storage_pin_card_if(&(storage_card_token_t){.epoch=card_epoch});assert(p>=0);int d=autoeq_directory(p,true,true);assert(d>=0);assert(!close(d)&&!close(p));path(profile,size,card,".local/autoeq/profiles/12345678.txt");}
static void unchanged_sound(unsigned version) {assert(settings_version==version&&peq_get_enabled()&&!strcmp(peq_current_preset(),"Body Voice")&&peq_get_preamp()==-33&&peq_bands[0].freq==1000);}
int main(int argc,char **argv) {
 assert(argc==3);path(card,sizeof(card),argv[1],"card");path(old_card,sizeof(old_card),argv[1],"old-card");path(internal,sizeof(internal),argv[1],"internal");assert(!mkdir(card,0700)&&!mkdir(internal,0700));change_owner();
 char config[2048];path(config,sizeof(config),argv[1],"settings.ini");put(config,"# fixture\n");assert(config_init(config));
 peq_preset_data_t original={.preamp_tenths=-33};for(int i=0;i<PEQ_BANDS;i++)original.bands[i]=peq_default_band(i);original.bands[0].freq=1000;assert(peq_preset_apply_parsed("Body Voice",&original,true));unsigned version=settings_version;
 job_t work=job();const char *which=argv[2];int before=fd_count();char file[2048];char *large=NULL;path(file,sizeof(file),card,"PEQ/Body Voice.txt");
 if(!strncmp(which,"host-",5)) {
  real_fs=true;
  if(!strcmp(which,"host-home-fallback")||!strcmp(which,"host-home-cache-ram")){home_case=true;path(host_home,sizeof(host_home),argv[1],"home");assert(!mkdir(host_home,0700));if(!strcmp(which,"host-home-cache-ram")){path(file,sizeof(file),host_home,".cache");assert(!mkdir(file,0700));ram_cache=true;}}
  if(!strcmp(which,"host-cold-index")||!strcmp(which,"host-home-fallback")){assert(update_index(&work)&&work.published&&network_calls==1);const char *cache=home_case?host_home:getenv("XDG_CACHE_HOME");assert(cache);int fd=open(cache,O_RDONLY|O_DIRECTORY);assert(fd>=0);struct statfs type;assert(!fstatfs(fd,&type)&&(uint32_t)type.f_type==0xef53u);assert(!close(fd));path(file,sizeof(file),card,".local/autoeq/INDEX.md");assert_text(file,index_text);}
  else {if(!strcmp(which,"host-ramfs")){real_fs=false;fault=15;}assert(!update_index(&work)&&!work.published&&!network_calls&&work.error[0]);if(!strcmp(which,"host-traversal")){path(file,sizeof(file),argv[1],"newprefix");assert(access(file,F_OK)!=0);}if(!strcmp(which,"host-symlink")){path(file,sizeof(file),argv[1],"outside/sonix");assert(access(file,F_OK)!=0);}}
  if(ram_cache){path(file,sizeof(file),host_home,".cache/sonix");assert(access(file,F_OK)!=0);}
  unchanged_sound(version);
 }
 else if(!strcmp(which,"download-profile")) {assert(download_profile(&work)&&work.published&&!work.error[0]);assert_text(file,valid_profile);unchanged_sound(version);assert(work.parsed.preamp_tenths==-62&&work.parsed.bands[0].freq==105);}
 else if(!strcmp(which,"rename-fail")||!strcmp(which,"write-zero")||!strcmp(which,"write-error")||!strcmp(which,"file-sync-fail")||!strcmp(which,"directory-sync-fail")||!strcmp(which,"close-fail")||!strcmp(which,"directory-close-fail")||!strcmp(which,"write-eintr")||!strcmp(which,"write-short")) {
  int p=storage_pin_card_if(&work.card);assert(p>=0);int d=directory_at(p,"PEQ",true);assert(d>=0);assert(!close(d)&&!close(p));put(file,"User preset\n");
  fault=!strcmp(which,"write-zero")?1:!strcmp(which,"write-error")?2:!strcmp(which,"write-eintr")?3:!strcmp(which,"write-short")?4:!strcmp(which,"file-sync-fail")?5:!strcmp(which,"directory-sync-fail")?6:!strcmp(which,"close-fail")?7:!strcmp(which,"directory-close-fail")?13:8;
  bool ok=download_profile(&work);assert(hit==1);unchanged_sound(version);
  if(fault==3||fault==4){assert(ok&&work.published);assert_text(file,valid_profile);}else {assert(!ok&&work.error[0]);if(fault==6||fault==13){assert(work.published);assert_text(file,valid_profile);}else {assert(!work.published);assert_text(file,"User preset\n");}}
 }
 else if(!strcmp(which,"unsupported")) {response="Filter 1: ON NOTCH Fc 1000 Hz Q 1.0\n";assert(!download_profile(&work)&&strstr(work.error,"unsupported"));assert(access(file,F_OK)!=0);unchanged_sound(version);}
 else if(!strcmp(which,"cleanup-refusal")) {
  int p=storage_pin_card_if(&work.card);int d=directory_at(p,"PEQ",true);assert(d>=0);assert(!close(d)&&!close(p));put(file,"User preset\n");
  char log[2048];path(log,sizeof(log),internal,"cleanup.log");int logfd=open(log,O_CREAT|O_EXCL|O_WRONLY,0600),saved=dup(STDERR_FILENO);assert(logfd>=0&&saved>=0);assert(dup2(logfd,STDERR_FILENO)>=0&&!close(logfd));fault=14;
  assert(!download_profile(&work)&&hit==2&&!work.published);fflush(stderr);assert(dup2(saved,STDERR_FILENO)>=0&&!close(saved));
  FILE *f=fopen(log,"r");assert(f);char text[1024];size_t n=fread(text,1,sizeof(text)-1,f);text[n]=0;assert(!fclose(f)&&strstr(text,"autoeq: temporary cleanup failed"));
  assert_text(file,"User preset\n");unchanged_sound(version);
 }
 else if(!strcmp(which,"old-token")) {swap_owner();assert(!download_profile(&work));assert(!work.published);assert(access(file,F_OK)!=0);unchanged_sound(version);}
 else if(!strcmp(which,"old-commit-token")) {work.profile_text=strdup(valid_profile);assert(work.profile_text);work.profile_size=strlen(work.profile_text);assert(peq_preset_parse_text(work.profile_text,work.profile_size,&work.parsed));swap_owner();assert(!commit_profile(&work)&&!work.published);assert(access(file,F_OK)!=0);unchanged_sound(version);}
 else if(!strcmp(which,"swap-owned-fd")) {swap_during_write=true;assert(download_profile(&work)&&work.published&&swapped);assert(access(file,F_OK)!=0);char old[2048];path(old,sizeof(old),old_card,"PEQ/Body Voice.txt");assert_text(old,valid_profile);assert(storage_card_try_check(&work.card)<0&&errno==ESTALE);unchanged_sound(version);}
 else if(!strcmp(which,"index-update")) {assert(update_index(&work)&&work.published&&!work.error[0]);path(file,sizeof(file),card,".local/autoeq/INDEX.md");assert_text(file,index_text);unchanged_sound(version);}
 else if(!strcmp(which,"internal-type")||!strcmp(which,"internal-space")) {fault=!strcmp(which,"internal-type")?10:11;assert(!update_index(&work)&&work.error[0]&&!network_calls);path(file,sizeof(file),card,".local");assert(access(file,F_OK)!=0);unchanged_sound(version);}
 else if(!strcmp(which,"stage-anonymous")) {int fd=internal_stage();assert(fd>=0);struct stat st;assert(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_nlink==0);assert(write(fd,"x",1)==1&&lseek(fd,0,SEEK_SET)==0);char c;assert(read(fd,&c,1)==1&&c=='x');assert(!close(fd));}
 else if(!strcmp(which,"stage-unlink-fail")) {target="/sonix-autoeq";fault=9;assert(internal_stage()<0&&hit);char parent[2048];path(parent,sizeof(parent),internal,"sonix-autoeq");DIR *dir=opendir(parent);assert(dir);int count=0;struct dirent *entry;while((entry=readdir(dir)))if(strcmp(entry->d_name,".")&&strcmp(entry->d_name,".."))count++;assert(!closedir(dir)&&!count);}
 else if(!strcmp(which,"index-too-large")||!strcmp(which,"index-stream-too-large")) {large=malloc(INDEX_MAX+2);assert(large);memset(large,'x',INDEX_MAX+1);large[INDEX_MAX+1]=0;response=large;unknown_length=!strcmp(which,"index-stream-too-large");assert(!update_index(&work)&&work.error[0]&&!work.published);path(file,sizeof(file),card,".local");assert(access(file,F_OK)!=0);}
 else if(!strcmp(which,"readonly-cached")) {int p=storage_pin_card_if(&work.card);assert(p>=0);int d=autoeq_directory(p,false,true);assert(d>=0);assert(!close(d)&&!close(p));path(file,sizeof(file),card,".local/autoeq/INDEX.md");put(file,index_text);sd_writable=false;network_ready=false;assert(load_search_results(&work)&&work.result_count==1&&!network_calls);assert(storage_pin_card_if(&work.card)<0&&errno==EROFS);unchanged_sound(version);}
 else if(!strcmp(which,"readonly-profile")) {char cached[2048];profile_directories(cached,sizeof(cached));put(cached,valid_profile);sd_writable=false;network_ready=false;assert(!download_profile(&work)&&work.error[0]&&!network_calls&&!work.published);assert(access(file,F_OK)!=0);unchanged_sound(version);}
 else if(!strcmp(which,"readonly-cold-search")||!strcmp(which,"readonly-cold-profile")||!strcmp(which,"readonly-update")) {sd_writable=false;bool ok=!strcmp(which,"readonly-cold-search")?load_search_results(&work):!strcmp(which,"readonly-cold-profile")?download_profile(&work):update_index(&work);assert(!ok&&work.error[0]&&!network_calls&&!stage_observations&&!work.published);path(file,sizeof(file),card,".local");assert(access(file,F_OK)!=0);path(file,sizeof(file),card,"PEQ");assert(access(file,F_OK)!=0);path(file,sizeof(file),internal,"sonix-autoeq");assert(access(file,F_OK)!=0);unchanged_sound(version);}
 else if(!strcmp(which,"cached-profile-no-rewrite")) {char cached[2048];profile_directories(cached,sizeof(cached));put(cached,valid_profile);network_ready=false;target="/profiles";fault=8;assert(download_profile(&work)&&work.cached_profile&&work.published&&!network_calls&&!hit);assert_text(cached,valid_profile);assert_text(file,valid_profile);unchanged_sound(version);}
 else if(!strcmp(which,"cache-clear")) {char cached[2048];profile_directories(cached,sizeof(cached));char parent[2048];path(parent,sizeof(parent),card,".local/autoeq/profiles");for(int i=0;i<40;i++){char f[2100];snprintf(f,sizeof(f),"%s/%02d.txt",parent,i);put(f,"cached");}assert(clear_profile_cache(&work));DIR *d=opendir(parent);assert(d);int count=0;struct dirent *e;while((e=readdir(d)))if(strcmp(e->d_name,".")&&strcmp(e->d_name,".."))count++;assert(!closedir(d)&&!count);unchanged_sound(version);}
 else if(!strcmp(which,"clear-refusal")) {char cached[2048];profile_directories(cached,sizeof(cached));put(cached,"cached");target="/profiles";fault=9;assert(!clear_profile_cache(&work)&&hit);assert_text(cached,"cached");}
 else if(!strcmp(which,"directory-symlink")) {char outside[2048],link[2048];path(outside,sizeof(outside),argv[1],"outside");assert(!mkdir(outside,0700));path(link,sizeof(link),card,"PEQ");assert(!symlink(outside,link));assert(!download_profile(&work)&&!work.published);path(link,sizeof(link),outside,"Body Voice.txt");assert(access(link,F_OK)!=0);unchanged_sound(version);}
 else if(!strcmp(which,"final-symlink")) {char outside[2048];path(outside,sizeof(outside),argv[1],"outside.txt");put(outside,"Sentinel");int p=storage_pin_card_if(&work.card);int d=directory_at(p,"PEQ",true);assert(d>=0);assert(!close(d)&&!close(p));assert(!symlink(outside,file));assert(download_profile(&work)&&work.replaced);assert_text(outside,"Sentinel");assert_text(file,valid_profile);}
 else if(!strcmp(which,"config-fail")) {peq_preset_data_t parsed;assert(peq_preset_parse_text(valid_profile,strlen(valid_profile),&parsed));config_fail=true;assert(peq_preset_apply_parsed("New profile",&parsed,true));assert(peq_get_preamp()==-62&&!strcmp(peq_current_preset(),"New profile"));config_fail=false;assert(config_save());}
 else if(!strcmp(which,"parser-oom")) {peq_preset_data_t parsed;memset(&parsed,0x77,sizeof(parsed));peq_preset_data_t saved=parsed;parse_oom=true;assert(!peq_preset_parse_text(valid_profile,strlen(valid_profile),&parsed)&&errno==ENOMEM&&!memcmp(&saved,&parsed,sizeof(parsed)));assert(!download_profile(&work)&&strstr(work.error,"out_of_memory")&&!work.published);unchanged_sound(version);}
 else if(!strcmp(which,"parser-compatibility")) {
  const char *inputs[]={valid_profile,"\xef\xbb\xbfPreamp: -5.0 dB\r\nFilter 1: OFF LSC Fc 100 Hz Gain -3 dB Q 0.8\r\n","Filter 1: ON NOTCH Fc 1000 Hz Q 1.0\n","No filters\n","Preamp: -200 dB\nFilter 1: ON HSC Fc 90000 Hz Gain 99 dB Q 0.01\n","Filter 1: ON PK Fc 1000 Hz Gain -2 dB Q 0.7\nChannel: R\nFilter 2: ON PK Fc 400 Hz Gain 5 dB Q 1\n"};
  for(unsigned k=0;k<sizeof(inputs)/sizeof(*inputs);k++){char input[2048];path(input,sizeof(input),internal,"parser.txt");put(input,inputs[k]);peq_band_t bands[PEQ_BANDS];int preamp=0,ignored=0;bool expected=reference_read_apo(input,bands,&preamp,&ignored);peq_preset_data_t actual;memset(&actual,0x77,sizeof(actual));peq_preset_data_t saved=actual;bool got=peq_preset_parse_text(inputs[k],strlen(inputs[k]),&actual);assert(got==expected);if(!got)assert(!memcmp(&saved,&actual,sizeof(actual)));else {assert(actual.preamp_tenths==preamp&&actual.ignored==ignored);for(int i=0;i<PEQ_BANDS;i++)assert(actual.bands[i].on==bands[i].on&&actual.bands[i].type==bands[i].type&&actual.bands[i].freq==bands[i].freq&&actual.bands[i].gain_tenths==bands[i].gain_tenths&&actual.bands[i].q_cent==bands[i].q_cent);}}
  unchanged_sound(version);
 }
 else if(!strcmp(which,"closing")) {job_closing=true;assert(!download_profile(&work)&&!network_calls&&!work.published);unchanged_sound(version);}
 else assert(!"Unknown case");
 free(large);free(work.profile_text);assert(fd_count()==before);puts("PASS actual token/AutoEQ/PEQ/config filesystem case");return 0;
}
