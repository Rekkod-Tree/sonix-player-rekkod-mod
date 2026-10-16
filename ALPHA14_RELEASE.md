# Alpha14: audio commands, metadata and experimental LTO

## Changes

Selected fixes from upstream `b1bf39d`, `ff9177b` and `3677092` were adapted to
the fork. Source commits: `b3d9048` and `18eef12`.

- A seek after paused suspend updates the saved fresh-restart position.
- Pause, seek, stop and replacement play wake the PCM wait through an eventfd.
  Commands are checked again before writing. Existing Bluetooth deadlines,
  anti-spin handling, S24 conversion, DoP and software/DSP paths are retained.
- Cancelled and partial writes restore unwritten seekable input. A decoder
  that cannot seek keeps its previous continuation behavior and logs a warning.
- WSOLA position accounting follows the delivered part of each output window;
  cached output is no longer charged in advance. Integer rounding is carried
  through partial pulls. The PCM algorithm and 1x/EOF paths are preserved.
- Metadata gains broader ID3/encoding support, native FLAC ID3 fallback,
  signature checks for known decoder extensions, AIFF/DFF/CAF tags and converted
  CUE targets. Fork-specific FLAC STREAMINFO/cover skipping, Ogg-FLAC fallback,
  ALAC source depth, Opus R128/ReplayGain priority and streaming sidecars remain.

Graphite components, gestures, fonts and palette source are unchanged. New
metadata rules apply when files are read/scanned; existing library rows are
not bulk rewritten as part of this release.

## Build and checks

The R1 image uses **experimental `TARGET_LTO=1`**, one final LTO partition and
the existing O2/O3 split. Default source builds still use `TARGET_LTO=0`.
No fast-math options or dependency upgrades were added. The complete host and
target builds produced no new compiler warnings; the final target passed the
mandatory glibc 2.22 ABI gate.

Validation:

- **36 normal-file checks on host and R1**: tags/source fields, leading ID3,
  cover-block skipping, renamed FLAC/ALAC/Opus, ReplayGain priority, sidecars,
  PCM/seek equivalence and CUE fallback. Fixtures are reproducible through
  `tests/metadata_normal_test.py`.
- Command/wait and cancelled-input regression executables passed on R1.
  These use production helpers with pipe-backed PCM/cursor fixtures.
- The WSOLA regression passed on R1 using actual target `speed.o`/`sha256.o`.
  Thirty complete PCM hashes/frame counts matched the previous implementation
  across rates, channel counts and speeds. At 1.5x/44.1kHz, 4096 delivered
  output frames account for 6144 source frames, replacing the premature 6354.
- Native UI scenarios passed at **480x800 and 480x720**, including 36 extra
  pages in both themes. This ran before the counter-only WSOLA follow-up;
  GUI source remained unchanged throughout.
- The extracted image passed chunk-chain/hash checks, binary/kernel matching,
  sealed-key checks, plaintext-key exclusion and test-runner exclusion.
  All **246** packaged font/module/shared-library entries matched alpha13.

## Live R1 smoke test

The final executable was temporarily run with a separate settings copy.
Two local FLAC tracks played through the existing MSEB/S24_LE path. Pause,
PCM release after pause, paused seek, resume at the selected position, playing
seek and next-track replacement worked. Playback health logs reported zero
write recoveries. Main-screen content below the status bar matched alpha13
pixel for pixel.

Candidate executable SHA-256:
`a73c6287172084313f91541ec89c73d1db3ab4aee087ea9021b061129a1cafee`.

The installed alpha13 executable was restored; its settings file was byte
identical, the boot ID stayed unchanged and temporary device files were
removed. The packaged alpha14 image itself has **not been flashed/booted**.

## Artifact and remaining acceptance

Supporting-workspace artifact: `firmware/1.3.0-ui3-alpha14/r1.upt`,
48,693,248 bytes, SHA-256
`95a1e2d9517911da0eb7ef2fbb002c05df60ac894b7737f27bf78f21bc6364ba`.
Its adjacent manifest records image checks and the executable smoke test.
Private logs, screenshots, exact symbols and check outputs are retained under
`output/alpha14-validation/` and `backups/r1-alpha14-20261017/`.

Full deep-suspend acceptance requires a later run without the charging/ADB
conditions that intentionally block suspend. Live streaming accounts and
external Bluetooth/USB-DAC output were not revalidated in this pass; R3 Pro II
hardware was not tested. PCM/cursor fixtures do not establish sample-exact
pause/resume for every tempo, queued-output and EOF combination. The LTO
release remains a test build pending longer complete-player use and an actual
packaged-image boot. Prior alpha13 microbenchmark gains are not an alpha14
whole-interface or battery-life measurement.
