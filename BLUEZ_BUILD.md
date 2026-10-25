# R1 Bluetooth daemon build

The R1 stock 2.0 daemon is BlueZ 5.54. Its A2DP reconfiguration can retain a
callback after releasing its reference, or run a delayed callback after freeing
the setup. Both failures are in the daemon, independently of BlueALSA/ALSA.

`sonix-player/tools/bluez/build.sh` rebuilds the same version from the
SHA-256-pinned official tarball. It does not replace system libraries or the
Bluetooth controller firmware. The source patch includes:

- [390c2b0](https://github.com/bluez/bluez/commit/390c2b0885bfb3d301a50b4959e50c31de52b91c):
  release the failed request's callback with its reference; also clean up sender rejection.
- [43ba14bd](https://github.com/bluez/bluez/commit/43ba14bdb34e972a0b72eafeb5d7273ba1fe9837):
  own, cancel and deduplicate the reconfiguration timer.
- Reject missing local/remote endpoints or codec capabilities through the existing error finalizer.
- Preserve HiBy's writable `/usr/data/bluetooth` pairing store, all 14 original
  built-in plugins, SDP compatibility and the A2DP-sink ACL priority command
  (HCI opcode `0xfd1a`, payload `handle, 1, 0`). The latter was checked against
  the original R1 2.0 daemon, SHA-256 `ba69c67d59a63e625bf32aab3fc7d1b4bdecee96f85e6b4acd3220034d685c20`.
- Make the existing byte-valued ATT link-type error conversion explicit;
  no change to integer-valued ATT errors or error values.

## Build and test

Use a complete MIPS glibc 2.22 sysroot with target GLib, D-Bus and ALSA headers,
libraries and pkg-config files. Never mix the host's headers/libraries with it.
Set `PKG_CONFIG_LIBDIR` to the target `.pc` directory; optionally set
`CROSS_PREFIX`, `ABI_PYTHON` (Python with pyelftools) and `JOBS`.

```sh
bash sonix-player/tools/bluez/build.sh STOCK_ROOTFS SYSROOT NEW_WORK_DIR NEW_OUTPUT_DIR
python3 sonix-player/tests/bluez_lifecycle_test.py --source NEW_WORK_DIR/bluez-5.54
```

For negative controls, add `--baseline` pointing to an unpatched 5.54 tree.
Tests extract production functions and use real GLib timers and callback
finalization under ASan/UBSan; transport operations are stubs. They do not
establish whole-daemon, radio, acoustic or other-device acceptance.

The build rejects compiler/linker warnings, checks actual target ABI, and records
source, patch, configuration, compiler, plugin and binary identities. Stage only
the tested `bluetoothd` at `usr/libexec/bluetooth/bluetoothd` in the R1 image.
For the packer, set `R1_BLUETOOTHD` to this file; its `.sha256` receipt is
required alongside it (or set `R1_BLUETOOTHD_SHA256_FILE`). A missing daemon or
hash mismatch stops R1 packaging instead of silently reusing stock BlueZ.
Keep debug symbols, logs and the manifest with local release artifacts. Do not
commit built binaries. Ship corresponding patched GPL sources/build instructions
with a release or provide them through the release's exact source commit.

Before packaging, exercise successful AAC/SBC changes, rejected changes,
disconnect/reconnect, AVRCP, pause/resume and sleep on R1. Confirm that the daemon
PID stays unchanged, pairings remain, output errors preserve track position and
normal/paused CPU usage does not regress. Other hardware/profiles need separate
acceptance; an ABI check alone cannot prove them.
