# BlueALSA 5.0 codec-bitrate extension

`sonix-player/tools/bluealsa/build.sh` applies the additive telemetry patch to
BlueALSA commit `1935d6dcb8975f2d7a51aaafe61538d157224623`. It exports the pinned Git
object, ignoring working-tree edits and platform line-ending differences. It also
applies `bluealsa-5.0-ldac-handles.patch`: the stock LDAC decoder and encoder
export incompatible implementations of the same handle create/free symbols.
Each direction resolves its own pair from its exact existing library during
codec setup, before transport workers start. Both directions remain available;
ABR, codec libraries and ALSA plugins are preserved. The two loader references
are bounded for the daemon lifetime; no lookup occurs per audio frame.
It builds only the daemon
for promotion; it does not install anything, replace libraries/plugins, flash
firmware or change the player. The API contract is in `BLUETOOTH_TELEMETRY.md`.

Use Linux with Git, autotools/libtool, pkg-config, make and the matching MIPS
toolchain. Obtain the pristine source from
[BlueALSA](https://github.com/arkq/bluez-alsa), and check out the pinned commit.
The sysroot must contain the device's real GLib, D-Bus, ALSA and codec libraries,
with matching development headers/pkg-config files. Preserve the existing LDAC
decoder compatibility setup used by the system-library build. Resolution uses
the existing system loader (`libdl`) and codec libraries; no runtime library is
installed or upgraded by this fix.

Set these inputs, then run the script:

```sh
export BLUEALSA_SOURCE=/path/to/bluez-alsa
export BLUEALSA_BUILD=/path/to/new/bluealsa-build
export CROSS_COMPILE=/path/to/toolchain/bin/mipsel-rockbox-linux-gnu-
export CC=/path/to/sysroot-aware-compiler-wrapper
export PKG_CONFIG_LIBDIR=/path/to/target/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=/path/to/target/sysroot
export RUNTIME_ROOT=/path/to/target/sysroot
export PYTHON=/path/to/python-with-pyelftools
bash sonix-player/tools/bluealsa/build.sh
```

The compiler wrapper supplies `--sysroot` and the runtime `-rpath-link` paths.
`AUTORECONF` may name a local autotools wrapper. The script rejects an existing
output directory and unsafe math flags; LTO uses `gcc-ar`/`gcc-ranlib` so archives
retain the required symbol index. It keeps the daemon's existing codec/profile
support, O2 optimization and writable `/usr/data` storage.

Artifacts are `bluealsad`, `bluealsad.debug` and `bluealsad.sha256`. Passing its
ABI check does not establish native acceptance. Verify the measured property,
old/new clients, teardown/reconnect, playback/idle CPU and normal sleep on R1.
Only then promote that exact daemon into the frozen R1 overlay and validate the
extracted image. Preserve the existing ALSA plugins, controller firmware and
libraries; do not copy the entire build/install tree over them.

Run `sonix-player/tests/ldac_handle_api_test.c` against `source/src/` from this
build and the real R1 codec libraries. Link the decoder before the encoder to
exercise the original collision. It checks distinct ownership, missing-library
and missing-symbol failures, repeated setup, quality switches, real encode/decode
and 256 bounded allocation lifecycles. Follow with repeated native PCM open/close;
a successful standalone test alone does not establish daemon acceptance.
