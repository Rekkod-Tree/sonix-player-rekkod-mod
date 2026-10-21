#!/usr/bin/env python3
"""Compile the verbatim production mount branch and uevent receive boundary.

Run from sonix-player: python3 tests/storage_remount_boundary_test.py
Optional first argument selects another system.c, for a negative control.
No mount, network, card, GUI or real directory mutation occurs.
Set SONIX_BOUNDARY_ONLY=mount or receive to isolate a negative control.
Storage callbacks are explicit fixtures: this proves their invocation/order in mount_sd,
not kernel hotplug delivery or the full storage manager's concurrent behavior.
Existing podcast state tests separately exercise real epoch/database changes.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / "src/system/device/system.c"
text = source.read_text()
start = text.index("static int mount_sd(")
end = text.index("\nstatic void unmount_sd(", start)
mount = text[start:end]
legacy = "bool runtime_recovery" not in mount.split("{")[0]
call = "mount_sd(&cfg, &runtime)" if legacy else "mount_sd(&cfg, &runtime, runtime_call)"
prefix = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
typedef struct { bool manage_mount; } storage_config_t;
typedef struct { int unused; } system_runtime_t;
static bool sd_requires_mount, sd_writable;
static char sd_root[512], sd_device_used[512];
static bool mounted, alive, attached, device_present, direct_ok, daemon_ok;
static bool runtime_call;
static unsigned epoch, detaches, unsafe_unmount, unsafe_mount;
static const char *pick_sd_root(storage_config_t *cfg) { (void)cfg; return "/fixture"; }
static const char *pick_sd_device(storage_config_t *cfg) { (void)cfg; return device_present ? "/dev/mock" : NULL; }
static bool is_mounted(const char *p) { (void)p; return mounted; }
static bool mount_is_alive(const char *p) { (void)p; return alive; }
static int mount_settled(const char *p) { (void)p; sd_writable = true; return 0; }
static void __attribute__((unused)) card_databases_detach(void) { if (attached) { attached = false; epoch++; detaches++; } }
static bool force_unmount(const char *p) { (void)p; if (runtime_call && attached) unsafe_unmount++; mounted = false; return true; }
static int count_entries(const char *p) { (void)p; return 0; }
static bool device_mounted_on(const char *p, char *out, size_t n) { (void)p; snprintf(out,n,"/dev/mock"); return true; }
static int mount_sd_directly(const char *d, const char *p) { (void)d; (void)p; if (runtime_call && attached) unsafe_mount++; if (!direct_ok) return -1; mounted=alive=true; return 0; }
static int sysserver_mount(const char *d, const char *p) { (void)d; (void)p; if (runtime_call && attached) unsafe_mount++; if (!daemon_ok) return -1; mounted=alive=true; return 0; }
static void list_block_devices(void) {}
static int mock_mkdir(const char *p, mode_t mode) { assert(!strcmp(p,"/fixture") && mode==0755); return 0; }
#define mkdir mock_mkdir
'''
suffix = r'''
#undef mkdir
static void scenario(const char *name, bool rt, bool m, bool live, bool prior_attached, bool direct, bool daemon, bool present) {
 storage_config_t cfg={.manage_mount=true}; system_runtime_t runtime={0};
 runtime_call=rt; mounted=m; alive=live; attached=prior_attached;
 direct_ok=direct; daemon_ok=daemon; device_present=present;
 epoch=100; detaches=unsafe_mount=unsafe_unmount=0;
 unsigned old_epoch=epoch;
 int rc=CALL;
 assert(!unsafe_unmount && !unsafe_mount);
 if (rt && (!m || !live) && present) { assert(detaches==(unsigned)prior_attached); assert(!attached); assert(epoch==old_epoch+(unsigned)prior_attached); }
 else assert(detaches==0 && epoch==old_epoch);
 assert(rc == (((m && live) || (present && (direct || daemon))) ? 0 : -1));
 printf("PASS %s epoch=%u detach=%u mounted=%d\n",name,epoch,detaches,mounted);
}
int main(void) {
 unsetenv("SONIX_NO_MOUNT");
 scenario("runtime missed-remove dead FAT mount, same volume identity",true,true,false,true,true,false,true);
 scenario("runtime missed-remove already absent mount",true,false,false,true,true,false,true);
 scenario("normal duplicate disk/partition add preserves playback",true,true,true,true,true,false,true);
 scenario("runtime already detached is idempotent",true,true,false,false,true,false,true);
 scenario("runtime mount failure remains detached",true,true,false,true,false,false,true);
 scenario("runtime daemon fallback already detached",true,true,false,true,false,true,true);
 scenario("startup never touches uninitialized card services",false,true,false,true,true,false,true);
 scenario("missing card cannot mount",true,false,false,true,false,false,false);
 return 0;
}
'''.replace("CALL", call)
# Use the actual production buffer declaration, receive expression and terminator.
decl = re.search(r"char buf\[4096\s*\+\s*1\];", text).group(0)
receive = re.search(r"int len = recv\(sock, buf, sizeof\(buf\)(?:\s*-\s*1)?, 0\);", text).group(0)
terminator = re.search(r"buf\[len\]\s*=\s*'\\0';", text).group(0)
boundary = r'''
#include <assert.h>
#include <string.h>
#include <sys/types.h>
static ssize_t recv(int s, void *out, size_t cap, int flags) { (void)s; (void)flags; memset(out,'x',cap); return (ssize_t)cap; }
int main(void) { int sock=0;
DECL
RECEIVE
assert(len>0);
TERMINATOR
assert((size_t)len < sizeof(buf) && len==4096 && buf[4096]=='\0');
return 0;
}
'''.replace("DECL",decl).replace("RECEIVE",receive).replace("TERMINATOR",terminator)
cc = shlex.split(os.environ.get("CC", "cc"))
flags = ["-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
with tempfile.TemporaryDirectory(prefix="sonix-storage-boundary-") as tmp:
 for label, program in [("receive",boundary),("mount",prefix+mount+suffix)]:
  if os.environ.get("SONIX_BOUNDARY_ONLY") not in (None, label): continue
  c=Path(tmp)/(label+".c"); c.write_text(program)
  exe=Path(tmp)/label
  subprocess.run(cc+flags+[str(c),"-o",str(exe)],check=True)
  subprocess.run([str(exe)],env=dict(os.environ,ASAN_OPTIONS="detect_leaks=1"),check=True)
  print("PASS production " + label + " boundary",flush=True)
if not legacy:
 assert text.count("mount_sd(storage_cfg, runtime, false)")==1
 assert text.count("mount_sd(storage_cfg, runtime, true)")==1
 assert text.count("mount_sd(g_storage_cfg, g_runtime, true)")==1
 assert text.index("podcastdl_cancel();",text.index("static void card_databases_detach(void) {")) < text.index("podcaststate_set_root(NULL);",text.index("static void card_databases_detach(void) {"))
 print("PASS all mount_sd callers and downloader detach ordering source checks")
