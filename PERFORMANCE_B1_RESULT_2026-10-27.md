# B1: bounded work, storage ownership and playback persistence

This wave implements the first, limited route from
[the performance plan](PERFORMANCE_SOLUTION_OPTIONS_2026-10-26.md).
It does not complete the entire audit or identify the cause of rare reboots.

## Result

| Area | Behavior now | Remaining boundary |
| --- | --- | --- |
| GUI bridge | A drain yields after eight callbacks or four milliseconds; latest slots and FIFO work share turns, and pending work re-arms the wakeup. | A single callback is not preempted. Slow SQL/network operations need their own follow-up. |
| File and playlist completion | A retained result slot survives a full GUI callback queue; shutdown joins/disposes owned work. | Other service adapters retain their existing contracts. |
| Worker-start failure | The file manager reports failure and releases busy/modal state without running its worker on the UI thread. | No claim that this was the observed native slowdown. |
| Card ownership | File mutations use an immutable request, the expected card token and pinned directory descriptors, including cleanup. Missing paths and storage errors are distinguished before database pruning. | No forced unmount or destructive physical-card tests. An interrupted tree copy can retain already completed destination files. |
| History and position | Sleeping bounded workers coalesce frequent updates; prepared close ends imports before draining accepted history. Offset-only queue saves after reopen check the exact row, count, type, epoch and generation in SQLite. | Whole queue/configuration/book writes are not all asynchronous. Scanner retirement remains a separate pre-existing risk. |
| Artwork and motion | Moving the player retains required surfaces, avoids repeated monochrome conversion, and skips unchanged background colors. Cover jobs apply the current priority after waking. | No global renderer change or cancellation of an already-running decode. |
| External PCM | A shared two-second no-progress budget and one same-format reopen bound retries. Live converter storage survives reopening; stop requests interrupt recovery. | Real repeated driver failure was modeled, not injected into the user's audio hardware. |
| Diagnostics | Optional elapsed histograms and input-to-next-render markers complement CPU time; all samples contribute to bounded histograms. | These are not optical input-to-photon measurements. Diagnostics are off in normal builds. |

## Verification

Tests use actual production code and SQLite where applicable, with ASan/UBSan
and negative controls that reintroduce the relevant defect. Important fixtures:

- `tests/gui_post_test.c`, `gui_completion_loop_test.py`, `library_completion_test.py`;
- `tests/filespage_worker_failure_test.py`, `fileops_card_test.c`, `filespage_card_context_test.py`;
- `tests/playhistory_async_test.c`, `playhistory_close_test.py`, `library_checkpoint_test.c`, `queue_epoch_rebind_test.c`;
- `tests/coverloader_priority_test.c`, `graphite_motion.inc`, `graphite_fileops.inc`;
- `tests/audio_external_recovery_test.py`, `perfstat_elapsed_test.py`, `perfstat_signal_test.py`.

Full LVGL scenarios passed at 480x800 and 480x720. R1 checks used the exact
target binaries, then restored the installed player and small configuration/DB
snapshots. Native file copy, move, rename, directory creation and deletion were
verified against actual file inventories and hashes. The external PCM path ran
normally, and its mocked-ALSA fixture also passed as a static MIPS executable.

The final code candidate (core10, player SHA-256
`fbb991c12db1e1270086584571d674240855a6b3f2b528db3479d15c60d1415a`)
passed MP3/FLAC playback over LDAC, Next/queue, screen off/wake, history display
and launching another item from history. A paused 2:26 display matched the
stored 146.471-second position and queue index 1. Four MIPS/SQLite cases passed
using private temporary databases: ordinary close, replacement, guarded queue
entry, and coalescing. No fatal, underrun, DB, card-I/O or audio-error messages
were found in this short candidate run.

Evidence receipts are in the local `output/performance-b1-20261026/` directory.
Secrets, runtime logs, database snapshots and firmware binaries are excluded
from the source repository. Training and final package acceptance are separate
from the code-candidate results above.

## Trained build and firmware acceptance

The frozen code was trained on R1 for 1,881 seconds. Thirteen observed scenario
records and nonzero arc counters in eleven key modules supplement the time
criterion. The fresh namespace contains 470 GCDA files, 2,768,028 bytes; prior
accepted profiles were preserved. Local FLAC/MP3, books, history, queue, catalog,
mini/player gestures, Yandex AAC, LDAC, screen off/wake and pause were exercised.
Other streaming providers and USB DAC were not separately trained in this run.

Strict USE passed with no unprofiled units, LTO/O3, no fast-math, and actual
glibc ABI validation. GENERATE took 2,385 seconds; USE took 638.6 seconds.
The final player is 8,559,724 bytes, SHA-256
`31263c5f38c02c6b94cd36808753c2d9d6546ac687f701c4b0c7151e6890e1f7`.
The normal-capability final binary passed native FLAC/LDAC, Next, screen off/wake,
pause persistence (2:01 = 121.208 seconds, queue index 10), edge Back, direct
book continuation and mini-player pull. Its short LDAC screen-off sample used
6.86% application CPU; history saving remained asleep. This is not a matched
before/after speedup or battery measurement. No fatal, underrun, DB, card-I/O
or audio errors were found in that smoke run. The installed program and small
test snapshots were restored, and temporary runtimes removed.

The R1 image passed extraction, executable hashes, OTA-chain/client fixtures and
resource checks. Only the player and build metadata differ from the prior core9
image; kernel and resources are preserved. Image size: 49,747,968 bytes, rootfs
headroom: 274,432 bytes. SHA-256:
`6f9d05950af4a2c21375e54282c9668035843dc934f8ef8244541f49aba0d0cd`.
Firmware installation/reboot and post-flash acceptance are not performed by
this temporary-runtime test.

## Measurement limits and next work

- The diagnostic queue-save tail fell from 12.117 to 0.343 ms and the position
  remembrance tail from 20.643 to 2.599 ms in the recorded scenario. These are
  component timings, not a claim that every track switch is that fast.
- Short matched wired screen-off samples remained around 5.5% application CPU;
  new save workers slept without periodic wakeups. The final core10 LDAC sample
  was 7.42% application CPU and is not directly comparable to wired output.
- Cached framebuffer rendering did not improve the measured Next tail and
  increased rendering CPU in the tested pair, so it was not enabled by default.
- Battery discharge, all RF conditions and long-duration reboot absence have
  not been established. Slow catalog I/O, broader resource budgeting, service
  cancellation and scanner retirement remain follow-up work in the dedicated
  Sonix backlog. Do not report the entire audit as complete.
