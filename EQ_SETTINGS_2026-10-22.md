# Coherent EQ settings publication

## Behavior and scope

EQ, PEQ and MSEB coefficient inputs now travel as one numeric snapshot with
the version belonging to that snapshot. Previously, a GUI edit during rebuild
could leave an old coefficient marked as the newest version indefinitely.
The GUI retains its editable values, curves, configuration format and presets.

Publication uses a short mutex around bounded numeric copying. Audio checks a
lock-free 32-bit version once per valid buffer and tries the mutex only when
that version changes. A busy mutex retains the previous complete snapshot;
audio does not wait for GUI or file IO. Rebuild, normalization and preamp all
use the accepted snapshot. A sample-rate change can rebuild the previous
snapshot and adopt the pending publication on a later buffer.

This preserves the existing single DSP-owner assumption. It does not establish
exclusive ownership across every output handoff, fix crossfeed ownership, or
explain Bluetooth stutters and rare reboots.

## Verification

- `sonix-player/tests/eq_settings_snapshot_test.py` compiles the actual source
  with deterministic interleavings, nine rejected protocol mutants and PCM
  comparisons against an explicitly supplied baseline. Normal and sanitizer
  runs pass, including rate changes and busy publication.
- Ten paired PCM cases are bit-identical, including nonzero low bits in S32,
  mono/stereo and rates from 32 to 384 kHz. Filter math and ordering are unchanged.
- Full host scenarios and target O3/LTO build pass; target ABI is checked against
  glibc 2.22. These diagnostic builds disable PGO.
- R1 component benchmark: 48 equal-work ABBA/BAAB measurements, identical input
  and output hashes. Active PEQ/combined CPU medians differ by under 0.06%.
  The all-off fast path adds about 51-59 ns per buffer (about 54-62% relative to
  its very small baseline). This is not a whole-player battery measurement.
- Full R1 application: AAC playback, live Body Voice preamp change, pause after
  DAC release, resume and seek pass. A paused 10.797-second interval records
  zero CPU ticks and context switches. Settings and three databases were
  restored; the boot ID did not change. No fatal pattern was found in the
  captured application/kernel logs.

Local evidence: `output/alpha26-final-20261022/` contains
`eq62-settings-snapshot/`, `eq62-native-ab/native/`,
`target-eq-snapshot-off/`, `host-eq-snapshot/`, `native-eq62-wave.json`
and `native-eq62-log-audit.json`. Bluetooth output acceptance, long sleep/soak,
rare-reboot diagnosis and final-image acceptance remain separate release gates.
