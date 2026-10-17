# R1 target LTO experiment

## Current build policy

Alpha15 was built with `TARGET_LTO=1`. Target firmware now enables LTO by default,
and all delivered builds must use it. Explicit non-LTO builds are retained only
for requested diagnostic comparisons. The measurements below describe the
original controlled alpha13 experiment.

## Original experiment decision

At the time of the comparison, LTO was available as
`make target TARGET_LTO=1 -j4` and disabled by default.
It reduced CPU time in a synthetic offscreen rendering workload by **5.23%**,
but increased the stripped player file by **1.13%**. This is a modest measured
gain, not evidence that the complete interface is 5% faster. No LTO firmware
was installed and no upstream features were mixed into the comparison.

## Controlled inputs

- Source: rekkod-mod `00888a2f803b720608f4a3d974bca37c10478e99` (alpha13),
  with only the optional build mode and standalone test additions.
- Compiler: Rockbox MIPS GCC 9.5.0, binutils 2.38; existing O2/O3 compile split.
  LTO adds `-flto=4`; final link uses
  `-flto=4 -flto-partition=one -O2 -march=mips32r2 -mno-madd4 -mplt`.
  No `-Ofast` or fast-math. Existing section garbage collection and stripping
  are retained, not credited as new optimizations.
- Dependencies: FreeType 2.14.3, Ogg 1.3.6, fixed-point Opus 1.6.1,
  opusfile 0.12 and WavPack 5.9.0, without LTO. Rebuilding changed archive/debug
  bytes; all 209 archive members in six versioned archives were identical
  after `objcopy --strip-debug`, including code, symbols and relocations.
- Hardware: real HiBy R1, installed alpha13. The live player kept running;
  test processes used priority 19. No framebuffer, input, ALSA, radio or card
  writes were performed by the benchmark. Fixtures/executables used `/tmp`.
  CPU frequency and temperature were not measured.

## Results

| Measurement | Ordinary target | LTO target |
|---|---:|---:|
| Stripped player, bytes | 8,764,580 | 8,863,540 |
| `.text`, bytes | 5,326,272 | 5,395,968 |
| `.rodata`, bytes | 3,233,392 | 3,263,920 |
| Rendering CPU ms/frame, median | 11.9625 | 11.3370 |
| Rendering CPU ms/frame, observed range | 11.939–12.001 | 11.287–11.356 |
| Rendering wall ms/120 frames, median | 1,472.973 | 1,388.957 |
| Benchmark max RSS, observed KiB range | 8,092–8,352 | 8,104–8,320 |

Renderer: actual LVGL and production UI Regular/Semibold plus fallback fonts,
480×800 RGB565 in RAM, rounded cards, text and an 18-row scrolling list.
Each process warmed up for 8 frames and timed 120 forced full redraws.
Six processes per variant ran in `ABBAABBAABBA` order. Every run checked full
flush count and scroll positions. All final frames had SHA-256
`2eacb6092a934396b6d340dbe62e49d7d06d91d4c2bd01c80e1d251ebc3fcda8`.
This checks the final frame, not every intermediate frame.

CPU timing uses `CLOCK_PROCESS_CPUTIME_ID`; wall timing uses monotonic time.
RSS belongs to the standalone benchmark, not the complete player. Its ranges
overlap; a memory saving is not established. Startup, gesture latency, real
screen FPS, streaming, standby, Bluetooth transport, USB DAC and battery life
were not measured with the LTO player. Rendering timings exclude font loading
and initial page creation. Existing resampler timing text uses wall time, so
it is not used here as a CPU-performance result.

## Correctness and compatibility

Both complete player builds passed `check-abi` (no glibc symbols beyond 2.22).
All six test executables per variant also passed ELF version checks before
being copied to the R1. On the device, each variant passed:

- Bluetooth rate-selection checks, with no Bluetooth connection required;
- software-volume arithmetic, resampling and interpolation checks;
- normal ALAC 16/24-bit mono/stereo fixtures against independent S32 PCM
  references: every decoded sample matched in all four cases.

These are standalone numeric/decoder checks, not listening or end-to-end
output tests. No malformed-media or fuzz workload was used. Player compiler
warnings were absent; dependency configure notices also occur in the baseline.
Build-policy tests checked LTO toggle/rollback, unchanged-build caching,
relinking after Makefile edits and unchanged host/launcher/dependency flags.

The first multi-partition LTO link failed the existing ABI gate:
`powf@GLIBC_2.27`. It was never run on the device. GCC 9 can separate the
translation-unit-local `.symver` assembly in `glibc_compat.h` from generated
math calls. A single final partition preserved `powf@GLIBC_2.0` without
changing the math functions. `tests/lto_abi_test.py` reproduces this across
eight C units and a C++ caller; `--diagnose-max` demonstrates the unsafe
multi-partition case on this compiler. Single-partition linking is slow and
does not get four-way final code generation from `-flto=4`.

## Artifacts and device state

Local evidence is in `D:\Projects\Mediateka\output\lto-20261016\` (directory
name reflects the experiment's start): full build logs, both player/debug
binaries, build manifests, ELF sections/segments, numeric logs,
`dependency-object-comparison.json`, 12 render logs and `render-summary.json`.
Ad-hoc orchestration scripts are alongside that directory in `output/`;
reusable benchmark and build/ABI tests are in `sonix-player/tests/`.

Player SHA-256 values:

- Ordinary experiment: `376801ad86b3eec325f9ce3d2aeb4669a1d22a32b74f61a809a6eecd3ddfab3b`.
- LTO experiment: `71780243a2fa8b49985405624da433cadd77d02f682c0ca982b7737e32d34e8b`.
- Installed alpha13, unchanged: `194b142b83f77c9472c07c065f4c56debd990e1e09bea901dc50fc849e3485b1`.

The installed player's PID remained 871. Temporary fixtures and both sets of
executables were removed, and their dedicated `/tmp` directory no longer
exists. No update image was flashed, no settings were changed, and no player
restart was requested. Release use still needs a complete-player hardware
comparison covering playback routes, streaming and sleep/wake.

## Next optimization work

Use this optional mode for a later complete-player A/B candidate. Prioritize
measured waits, UI-thread work and redundant redraws before introducing more
compiler switches. Upstream's interruptible ALSA waiting addresses a concrete
latency source separately. PGO is a possible later experiment, requiring a
representative workload and safe handling of profile writes; it is not enabled.

Reference: [GCC optimization and LTO options](https://gcc.gnu.org/onlinedocs/gcc-9.3.0/gcc/Optimize-Options.html).
