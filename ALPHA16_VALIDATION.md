# Alpha16 validation

**Status, 2026-10-17: revised USB build packaged and extracted; 18 operational
comparison runs and final native USB/UI checks complete; packaged-image boot
pending.** Instrumented and optimized candidates ran temporarily on the R1.
The installed alpha15 executable has not been replaced. This is an alpha build,
not a claim of universal acceleration or full firmware acceptance.

## Native interface checks

The completed host UI executable passed the upstream integration suite at
480x800 and 480x720, in dark and light themes. Coverage includes PEQ numeric
entry, application and cancellation; mini-player visibility; lyrics results and
preview; image loading and failure; history; metronome; recovery identification;
queue selection, removal, Undo, grip dragging, and Ask/Next/End enqueue policy.
Evidence is in workspace `output/alpha16-ui-{800,720}/run.log`.

The component suite also passed both heights, including dark/light and large
text variants: filled choices, note hierarchy/details, date/time cancellation,
and sixteen quick controls. See `output/alpha16-components-{800,720}/`.

A separate 480x720 run used large Segoe text (`ui.text_size=1`) and fresh private
mount namespaces. It exposed two visual defects: stale metronome Play ink after
a theme change, and a dragged row clipped by the short queue body. Both were
fixed. The repeated suite passed new assertions for current accent ink, held-row
bounds, opaque drag fill, and restoration of transparency after release. Visual
comparison confirmed both fixes. The 38 new captures are in
`output/alpha16-upstream-large-720-fixed/`; original defect captures were retained.

## Spoken media and regression checks

`tests/run_spoken_ui.py` passed at both heights against local Yandex and Zvuk
mock servers. It exercised typed audiobook/podcast search, complete thirteen-part
book downloads through UI actions, native Audiobooks indexing, ordered parts,
MP3 decoding, player paths and resume. Podcast catalogues contained 338/299
episodes, with dates, first/last pages and download menus. Zvuk hydrated only
88 distinct episodes of 338, with a maximum batch of fifty. These checks used
synthetic sessions and disposable state; they do not verify live service rights
or availability. Logs, hashes and 34 captures are in `output/alpha16-spoken-ui/`.

Eight isolated regression executables passed: podcast import, imported shelf
names, audiobook import/resume, metadata state, Yandex metadata jobs, Zvuk
metadata jobs, storage handoff, and the recovery helper's twenty-four offline
scenarios. The metadata-state fixture required an update for the new history
hook; its checked stubs verify one matching path/title/artist notification.
The initial link failure and subsequent pass are both retained. Commands,
source hashes and exit codes are in `output/alpha16-regressions/`.
Standalone `-Wextra` diagnostics remain recorded; these results are not a claim
that every full-build configuration is warning-free.

## USB fixes and safety checks

Native traces exposed three causes: missed brief configuration events, a stalled
controller setup queue, and Bluetooth helpers retaining an inherited FLAC file
after exec. The revised watcher uses bounded kernel-event wakeups, checks live
ownership again, and permits one guarded restart of an empty reader. It never
uses forced/lazy unmount or exports from a cached event. The common child-launch
boundary marks inherited descriptors close-on-exec while preserving caller
stdout redirection and the private logging relay.

`usb_host_test.c` passed ASan/UBSan and `-Wall -Wextra -Werror`: stale/spoofed
events, exact tokens, bounded waits, suspended-state evidence, missing/busy LUN,
owner/ADB/DAC changes, unplug and bind failures. `storage_handoff_test.c` passed
ordinary-unmount, release, mount/update failures, cancellation and ownership
checks. `child_exec_fds_test.c` passed actual fork/exec checks for file/socket
closure, unchanged parent handles and working child stdout/logging.

On the R1, fresh Bluetooth services held no card descriptors even while the
parent player had an open FLAC decoder. Three generation-candidate USB cycles
reached the Windows R1 disk, safe host eject and local-card/ADB restoration.
Evidence: `output/alpha16-usb-cycle-20261017-{192207,192412,200133}/` and the
archived `bluetooth-fd-proof.json`. The final cycle initially exposed a host-test
race: a physical disk can appear before its drive letter. The test now waits
for the exact R1 volume; safety checks in the player were not relaxed.

The final optimized executable also passed a native USB cycle. Windows read
the card's root directory (11 entries), then safe host eject returned the card
and ADB. The 6.219 s host-access observation includes script setup and Windows
polling; it is not controller-only latency. Playback of the same FLAC24 track
resumed from its saved position afterward, with correct metadata and queue.
Evidence: `output/alpha16-usb-cycle-20261017-213018/`.

Standalone optimized UI checks covered MP3 launch from Files, a shuffled
44-track folder queue, Move Next and the resulting FLAC24 track/title/cover,
removal and Undo, and search history with track/album/empty-artist results.
The six-second Undo notification required a frame-checked native test: an
earlier late manual tap was invalid and was not counted as a pass or UI defect.
The controlled repeat checked the button twice, restored the upcoming count
from 35 to 36 and produced the native `queue: undone (43 tracks)` log.

Alpha15 was restored without reboot; original settings matched, with standby
1 min, auto-off 30 min and screen-off 30 s restored through the UI. Owned
temporary files were archived/verified and removed. No new storage or kernel
faults were found. Repeated pre-existing FAT warnings are recorded separately.

## Optimization and driver boundaries

The current source digest is
`125c29daf2fb2ed410ae9e5899768d74913d551c864be0732bb909a42509caea`.
Fresh target training produced 456 RAM profiles, 2,624,568 bytes, aggregate
SHA-256 `7d3520a7612fda043cf1b10ed57b80391e0518c8a87ad3d727122d04129fe014`.
The final snapshot was copied only after trainer termination and original-player
restoration, with identical remote hashes before and after transfer. An earlier
live copy changed and was preserved separately. Earlier long USB-debugging
profiles and profiles from different sources were excluded.

Training covered FLAC play/next/seek, MP3 launch from Files, folder shuffle,
queue Move Next and correct subsequent title, RU/EN search and filters,
downloaded MP3/AAC audiobook resume, themes/gray accent, brief flat PEQ playback
and one successful native USB cycle. Four normal ALAC clips exercised the exact
generation decoder objects with bit-exact reference PCM. Native Opus, FLAC24,
DSD, USB-DAC and live service playback were not trained. The recipe and profile
inventory are retained in workspace `output/alpha16-pgo-training.json`.

Both current-code variants built without new warnings and passed ABI <=glibc
2.22. A uses LTO/O3 without PGO; B uses the same source with fresh PGO, fatal
missing/mismatching profile checks and no fast-math. Each passed eight native
numerical checks: Bluetooth rate selection, software volume, resampling,
interpolation and four ALAC 16/24-bit mono/stereo PCM comparisons. The production
binary contains neither the profile-dump handler nor the UI-test runner.

The operational comparison uses A, B and the historical accepted PGO executable
C (source `6403a96d...`). `(A B C C B A)` repeated three times gives six runs per
variant and ABBA ordering within each pair. Settings, font resources, output and
effects are held fixed. Validation uses Morphine's *Cure for Pain*, distinct
from the training playback corpus. Each action has a native frame and monotonic
timings; ready-page measurements require an expected stable text region.
Play/Next PCM measurements exclude writes before the intended file's start.

All 18 runs passed the completeness/identity/settings/route/ROI checks: six
per variant, with no excluded formal runs. Median [min-max] times in ms:

| Observable | C: previous PGO | A: current O3 | B: current PGO |
| --- | ---: | ---: | ---: |
| Album-list text ready | 348.2 [335.0-382.8] | 371.9 [349.1-422.3] | 357.0 [335.8-368.4] |
| Album title ready | 372.7 [364.1-385.9] | 414.6 [404.1-431.1] | 365.7 [343.9-384.7] |
| Player title ready | 256.4 [233.7-271.4] | 258.2 [250.0-279.1] | 265.7 [236.9-288.2] |
| Cold Play to target PCM | 210.0 [189.5-214.7] | 207.2 [196.4-238.3] | 218.9 [201.5-238.3] |
| Next to target PCM | 176.6 [161.2-185.4] | 186.2 [174.3-195.0] | 178.7 [169.4-185.4] |
| Seek to next write (proxy) | 58.1 [52.2-63.9] | 57.6 [54.8-64.2] | 53.7 [52.4-58.6] |

The five-second Play window used median 25.72/29.06/24.94% of one core for
C/A/B, respectively. Target RSS after that window was 19,322/19,898/19,266 KiB
with the observer loaded; this is not total-device or peak memory.

B/A album-title readiness improved 11.784% and Next PCM 4.022%. Cold Play
instead increased 5.672%, **11.751 ms**. Its overlapping ranges do not prove
absence of a regression: the result crosses the suggested 5% investigation
trigger and remains a cold-start optimization/acceptance concern. The fixed
comparison disabled remembered-track restoration, whereas training started by
restoring a paused track; this is a hypothesis to investigate, not an excuse
to discard the cold result. Player-title readiness also did not improve.

Current offscreen rendering used separate ABBA x3 comparisons with identical
pixels (`2eacb6092a934396b6d340dbe62e49d7d06d91d4c2bd01c80e1d251ebc3fcda8`).
A/B CPU for 120 frames was 1354.913/1180.202 ms, a 12.895% reduction (about
1.46 ms/frame). C/B was 1157.003/1175.275 ms, **1.579% slower** for B (about
0.15 ms/frame). The latter is retained explicitly; improvement over an
unprofiled build does not establish improvement over the earlier PGO build.

Raw runs and the completeness-checked summary are in workspace
`output/alpha16-perf/`; renderer data is in `output/alpha16-bench-results/`.
A/B isolates PGO; B/C includes code changes. The roughly 224 KiB shared trace,
controller CPU cost, 20 ms ROI polling and 10 ms target CPU ticks are explicit
limits. Ready-title timing does not prove that every image finished loading.
PCM submission is not acoustic output; a post-seek write does not prove the
sought position. No analog quality, battery-life or application-wide speedup
claim follows from these tests.

Historical offscreen results are retained separately: the earlier O2/O3
comparison saved 1.0439% CPU; the previous PGO renderer saved 14.3051%, about
1.6 ms per frame in that workload. They are not results for this revised build
and do not establish perceptible application-wide improvement.

The previous R1 `codec_cs43131`/`soc_aic` pair is retained. Static inspection
confirmed that upstream's replacement still advertises S24_LE, but its new
stop fade modifies DMA words as PCM before muting and does not distinguish DoP.
That replacement was therefore excluded rather than silently reducing support.

## Storage, recovery and remaining acceptance

The pre-candidate device baseline contains an existing FAT unclean-unmount
warning at boot timestamp **3.420584**, already present before candidate testing. This warning
alone proves neither current corruption nor causation by alpha16. The captured
baseline is in `output/alpha16-device/pre-candidate-baseline.json`.

The public recovery wizard identifies supported images, verifies backups and
requires separate confirmation. Unknown recovery remains blocked. The first
transition from an older unguarded firmware still uses its old recovery; an
already guarded alpha15 installation with unsupported recovery needs a separate
supported transition. See [Safe R1 updates](RECOVERY_R1.md); renaming an image
does not bypass the guard.

The revised 49,647,616-byte R1 package was extracted and verified: chunk chains,
declared sizes, exact executable/kernel/font resources, LF boot scripts, all
24 locale files, sealed keys, recovery helper and private repair block. SHA-256:
`86e40b3ca730a33cb245f93468b50ae0a18325742b154117e821fe0ff9eacf27`.

Remaining release gates include a booted-image check, first-transition recovery
validation on a supported unmodified device, and resolving/accepting the measured
cold-start tradeoff before claiming regression-free optimization. R3 Pro II,
USB-DAC, analog output, charging and battery runtime were not hardware-accepted.
Restoring standby settings is not a suspend/resume acceptance test. The card's
pre-existing FAT flag still needs an offline check if filesystem health is to
be accepted; successful USB handoff does not replace filesystem validation.
