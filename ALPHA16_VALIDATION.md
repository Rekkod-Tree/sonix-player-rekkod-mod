# Alpha16 validation

**Status: built and verified, 2026-10-17; packaged-image boot pending.** The
instrumented and final optimized candidates ran temporarily on the R1. The
installed alpha15 player was restored afterward without rebooting or replacing
its executable. Full-application PGO training, strict profile-use linking, ABI
checks and package extraction checks passed. No packaged alpha16 image has been
booted; executable and fixture checks are not complete firmware acceptance.

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

## Optimization and driver boundaries

An earlier O2/O3 offscreen-renderer comparison used ABBA repeated three times,
the same fonts, and identical final pixel SHA-256. Reported CPU time decreased
from 1365.178 ms to 1350.927 ms, **1.0439%**. The compared binaries grew from
9,079,060 to 9,084,532 bytes, an increase of 5,472 bytes. These are measurements
of that synthetic rendering workload and those earlier builds; they establish
neither application responsiveness nor battery-life improvement. Evidence:
`output/alpha16-bench-results/render-o2-o3-summary.json` and build manifests.
Both benchmark variants also passed btrate, software-volume, resampling,
interpolation and four normal ALAC reference checks.

A small MIPS probe passed generation without LTO followed by profile use with
LTO and fatal coverage mismatch checks. The same workflow then produced a
warning-free instrumented application with the required glibc <=2.22 ABI.
It collected 456 RAM profiles (2,623,040 bytes), including verified post-start
metronome activity and four normal ALAC clips linked to the exact instrumented
decoder objects. Profile hashes were checked before removal from the device.
Sources are frozen at digest
`6403a96df61f99d67d5b91ea4e081d95013566a25c23514f2eca6aae5fa447e1`.
The production build retains LTO/O3 and treats both mismatching and missing
profiles as errors. Final optimized linking completed without new warnings and
passed the glibc <=2.22 ABI check. The production executable has no training dump
handler, profile-output directory or UI-test runner.

On the actual R1, an O3/PGO ABBA comparison repeated three times reduced median
rendering CPU time from 1347.401 ms to 1154.654 ms for 120 frames (**14.3051%**).
Final pixels matched exactly. Median maximum RSS was 7250/7320 KiB; this is not
a memory-saving claim. The executable shrank from 9,084,532 to 8,631,324 bytes
(about **5%**). All eight numerical checks passed again with the final PGO
objects. Evidence: `output/alpha16-bench-results/`. These results concern the
tested rendering workload, not whole-application speed or battery life.

The temporary R1 run exercised album browsing, local FLAC playback/next/seek/
pause, queue reordering and Done, history, edge-back and player-collapse gestures,
light/gray appearance, date/time cancellation, PEQ numeric application and
restoration, and metronome start/stop. The installed executable was not replaced.
Original settings matched afterward except normal clock bookkeeping; no new
storage, OOM or kernel-fault messages appeared. Wi-Fi remained off, so no live
network playback or USB-DAC profiling is claimed. Diagnostic overhead is not a
measurement of production performance.

The final optimized executable also passed R1 startup, local FLAC playback,
next-track metadata, pause and queue navigation. Both framebuffer pages were
captured read-only when checking suspect row alignment; repeated complete
captures did not reproduce the apparent overlap. External captures of a stale
or partially synchronized framebuffer must not alone establish a UI defect.
Original settings matched after restoration except normal clock bookkeeping;
no new storage, OOM or kernel-fault messages appeared.

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

The final 49,590,272-byte R1 package was extracted and verified: chunk chains,
declared sizes, exact executable/kernel/font resources, LF boot scripts, all
24 locale files, sealed keys, recovery helper and private repair block. SHA-256:
`674d6fab6e88edb3a5d5a093945bcefc0b4a7f0329b8b86dec4e44adf924fe45`.

Remaining gates include a booted-image check and first-transition recovery
validation on a supported unmodified device. R3 Pro II
hardware, physical USB/USB-DAC behavior, analog output, charging, standby and
battery runtime are **not accepted** by the host and benchmark evidence above.
