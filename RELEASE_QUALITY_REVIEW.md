# Release quality review

## 16 October: alpha13 safe-update validation

The user installed alpha13 through the patched recovery after the clean card baseline. On the resulting normal boot the installed executable and card image matched their expected hashes; fresh kernel messages had no unclean-FAT warning, invalid cluster, I/O error or read-only remount. FAT32 remained writable, the recovered FLAC matched its verified hash, and 695 library tracks opened. Recovery persistence passed independent whole-image/block checking with zero bad blocks/ECC failures. The tested SD update cycle is accepted; repeated cycles, the alpha13 pre-arm path on a subsequent update and unrelated USB update media remain separate from this one-cycle evidence.

Read-only extraction of the inactive R1 recovery confirmed a separate defect: `recoveryd` mounts SD read/write and calls the reboot syscall after `sync()` without unmounting the card. The normal `.upt` does not replace this recovery. `RECOVERY_R1.md` documents both boundaries, the reviewed one-byte read-only mount candidate, the separately authorized repair utility, backups and remaining acceptance.

The application now refuses to arm recovery unless card sync/ordinary-unmount succeeds, restores services after a pre-arm failure, verifies the boot selector, and stops automatic restart/idle auto-off when Normal restoration cannot be confirmed. An R1-specific full raw recovery hash guard prevents use of an old or unknown update module. Failure messages are present in all 22 language files. This guard intentionally requires the one-time reviewed recovery repair before subsequent R1 updates.

Host/MIPS builds and the glibc 2.22 ABI check pass without compiler warnings. Native core UI checks pass at both panel sizes (58 captures each). Storage handoff/preparation and 18 firmware-flow fixtures pass ASan/UBSan. The separately built repair helper passes 24 offline scenarios using actual old/new image/block hashes. Independent review identified and resolved a concurrent-helper race with a retained device lock. Both the real-device isolated RO-loop test and actual read-only repair preflight passed. After explicit user approval the one-block repair was applied, the full raw image matched the reviewed fixed SHA-256, and independent post-apply checking passed with zero bad blocks/ECC failures. Normal boot and alpha12 remained unchanged during the repair. A subsequent PC check found no structural FAT errors, verified preserved files, and confirmed a clear dirty flag after clean dismount; alpha13 was verified on the card and safe eject confirmed. The later update cycle passed as recorded above. Wider energy/audio/live-account release acceptance remains open.

## 16 October: alpha12 storage, search and standby corrections

USB storage no longer exports a card after a failed unmount, nor uses forced/lazy unmount as an export prerequisite. It requires a known unmounted state, including whole-device/partition overlap checks. USB, hotplug and the storage supervisor share the existing card lock; the host's backing file must be confirmed released before local mounting resumes. Failed remounts leave card services offline rather than opening databases or logs on the internal mount directory. A separate configured mount-point accessor preserves raw USB access for repairing a card that cannot mount. ADB keeps the card local.

Yandex and Zvuk permanent-save jobs hold an open card-directory descriptor across HTTP waits and all writes. Ordinary USB unmount therefore refuses while such a job still owns the card. Cancellation is checked after network replies and before output/commit; the copied root cannot change underneath directory creation. Progress callbacks use inexpensive state checks rather than scanning mounts every 64 KiB. This closes the reviewed late-writer scenarios; it is not proof against every physical-removal or power-loss failure.

Held startup/USB log output now stops growing at a 32 KiB threshold. The normal single-writer path enforces this size; concurrent direct fallback writes can briefly overshoot it, so this is not a strict cross-process quota. Existing SD log rotation remains at 8 MiB plus one previous file. Standby now waits for library/audiobook scans, including scans started after card insertion or manual screen-off. No new polling timer was added, and no battery-runtime gain is claimed.

The integrated host/MIPS builds pass without compiler warnings, including the glibc 2.22 ABI check. Core native UI tests pass at 480x800 and 480x720 (58 captures each), as does the separate current-track permanent-save regression using the local mock service. USB/storage and both providers' late-save fixtures pass AddressSanitizer/UndefinedBehaviorSanitizer; both cache variants pass AddressSanitizer, and all 13 logging scenarios pass. Independent review has no blocking findings. Evidence: workspace `output/native-ui-alpha12-{core,save}/`, `output/alpha12-*-test.log` and `output/sonix-graphite-build.log`.

The packaged alpha12 image booted on R1 and its installed executable matched the build. Targeted on-device search/history checks passed; see `UI3_VALIDATION.md`. The card stayed FAT32 read/write with about 8.2 GiB free. The post-update kernel log reports an unclean-unmount flag, present before the application starts, but no new invalid-cluster, I/O-error or read-only-remount message during this check. This flag needs investigation across the firmware-update/recovery path; it alone does not demonstrate fresh FAT damage. Physical alpha12 USB handoff, standby/resume, sustained playback and battery discharge remain unverified. Live-account coverage, R3 Pro II and KA17 acceptance also remain open. Existing short idle-work measurements are not battery-runtime acceptance.

Separately, the user's FAT32 card was fully imaged before an offline Windows repair. The recovered full audio file and rolled-back database were restored with hash checks; the final read-only filesystem check returned no errors and the reader was safely ejected. Recovery files and the original image were retained. This establishes the PC-side repair, not the card's hardware health or a confirmed cause. Private evidence is in workspace `backups/r1-card-20261016/RECOVERY_RESULT.md`.

## 16 October: alpha11 candidate and card diagnosis

The search layout, Recent heading and T9 field-switch corrections are documented in `UI3_VALIDATION.md`. Independent review also identified two streaming-cache defects: the caller could read a worker-owned path after the worker freed it, and output-write errors lost their cause. Both Yandex and Zvuk now copy the returned path before starting the worker and preserve write/flush/close/rename errors. `tests/stream_cache_io_test.c` includes the production implementations and covers successful output, full storage, fast worker completion and failed thread creation; both service variants pass AddressSanitizer. Async playback still uses its existing generic error message, while the log retains the cause.

The main SD log now rotates at 8 MiB with one previous log. The existing writer performs rotation; ordinary writes do not hold the destination mutex. `tests/logging_rotation_test.c` passes 11 cases, including USB handoff, contention, failed storage operations, oversized old logs and explicit `SONIX_LOG` overrides. Migrating an already oversized log temporarily needs up to 8 MiB extra storage and holds the destination lock while copying its tail. If this cannot be saved, the original is retained and new excess output is dropped. Startup/USB-held output can briefly add up to 32 KiB beyond the active threshold before writer normalization. The separate held-output tmpfs growth risk remains open; this change bounds the normal SD destination.

Read-only inspection of the connected alpha10 R1 found **8.40 GiB free** on a 29.26 GiB FAT32 card, 71% used. The main log was about 262 KiB, and both service cache directories were empty. No current storage exhaustion is established. However, the preserved log contains 20 invalid FAT cluster-chain errors and an explicit read-only remount at `2026-10-16 09:55:43` device time (UTC). That state prevents either service from writing its cache. The card is currently writable after a later boot, but current kernel messages still report an unclean previous unmount; filesystem health has not been established. Important files need backup and an offline FAT check before using this card for firmware installation. No repair, formatting, cache deletion or radio-setting change was performed in this inspection. Private evidence is in workspace `output/diagnostics-20261016/`.

Wi-Fi startup emits transient control-socket/duplicate-interface messages followed by scan acknowledgements; missing `wlan0` during parked-radio sleep alone is not a proven Wi-Fi fault. An older log session records roughly five-second UI stalls with the log writer waiting on dirty-buffer I/O and no system idle time. The new fixes are not claimed to resolve those historical stalls. Additional review notes: already-loaded row thumbnails can remain stale after replacing artwork; large-library synchronous search latency requires measurement.

## Stock HiBy OS 2.0 assessment

HiBy's [official R1 2.0 changelog](https://store.hiby.com/apps/help-center#hc-r1-firmware-v20-update) lists PEQ, games, scheduled pause, playlist changes and unspecified fixes. On 16 October the entry directs users to OTA and provides no separate image link. No 2.0 image has been obtained or compared here.

The current local stock input matches the retained 1.6 image byte for byte (SHA-256 `9aada81995d8d2b2ed80d6cf292c62bc5f0f705e51e4f69c7e766ee67536ba60`). The packer removes `hiby_player` and its UI assets, overlays Sonix system files and drivers, and installs the supplied Sonix R1 kernel. Consequently, changing the stock input does not automatically import HiBy's application features or all of its driver fixes. Sonix already implements its own PEQ, games, playlists and sleep timer.

Decision: keep the verified current base for alpha11. A 2.0 rebase is useful only after comparing the actual root filesystem/kernel/modules and identifying retained system fixes, followed by ABI, boot, suspend, radio and audio checks. Updating just the stock filename would not establish a benefit or compatibility.

## Earlier review baseline

Updated 15 October 2026. The R1 now runs alpha9 (`151020260143`), from `main` at `d3d5842`. UI and idle-work changes are committed in `73b52bb`, atomic software volume in `1c98cb5`, decoder bounds in `f8958c6`, and versioned dependency builds in `d3d5842`. Alpha10 restores the approved small-text rasterization. A completed build or targeted hardware check does not close the wider release review.

## Evidence and acceptance still required

| Area | Current evidence | Work remaining before a release recommendation |
|---|---|---|
| UI fidelity | Four alpha8 findings corrected and checked on R1; common, component and targeted regressions pass at both panel sizes. Alpha9 small-font difference reproduced and isolated; see below. | Boot the final alpha10 image and confirm complete installed-resource behavior. R3 Pro II hardware remains unavailable. |
| Reliability | Atomic software-volume snapshots and decoder bounds committed. Native R1 volume/concurrency, ALAC PCM and WavPack format checks pass. | Sustained playback and repeated standby/storage transitions are not yet accepted. Per the user's request, damaged-input tests are not being repeated. |
| Responsiveness | R1 event-driven input and unchanged-shade repaint suppression verified, with comparable diagnostic measurements below. | Full navigation/cover-loading latency distribution remains unmeasured. |
| Energy | Idle loop frequency and redundant redraws reduced; short baseline samples show stable RSS. | Battery runtime and a long playback/standby soak remain unverified. USB power prevents an autonomy claim. |
| Audio | Digital filter/policy checks, native concurrent volume checks, bit-exact ALAC and WavPack PCM comparison pass. New Opus/WavPack target CPU measured below. | Live effect activation can retain the 16-bit processing path until the next track. USB KA17 negotiation/output and analog THD/noise are unverified; the user declined manual DAC testing. |
| Architecture/kernel/dependencies | Versioned static archives, SHA-256 checks and consumer flag stamps implemented. Updated audio libraries run on MIPS. Existing device kernel/ABI retained. | Legacy kernel and system TLS/library migration remain separate compatibility work. No claim of a complete platform upgrade or audit. |

## Initial device baseline

Evidence: workspace `output/quality-review/alpha8-idle-screen-{on,off}.txt` and `baseline-summary.json`. Each sample spans approximately 30 seconds. Counters are process ticks divided by total CPU ticks, not a profiler estimate or battery measurement.

| State | Player CPU | System busy | RSS start/end | Main-thread voluntary switches |
|---|---:|---:|---:|---:|
| Paused, screen on, brightness 63 | 3.341% | 5.479% | 21,224 / 21,224 KiB | 2,729 |
| Paused, screen off | 1.165% | 2.097% | 21,244 / 21,244 KiB | 342 |

The main/UI thread and USB watcher account for most sampled work. The stock `axp_battery` interface reports zero voltage/capacity while the separate battery interface reports 4.392 V and 100%; USB power is connected. Do not infer discharge current or runtime from these inconsistent interfaces. The screen was restored to brightness 63 after the measurement.

## Existing audio checks

`output/quality-review/run_dsp_checks.py` compiles and runs repository tests. `resample-test.log`, `interp2-test.log` and `btrate-test.log` record passes. These test digital filters and policy; they do not capture the final analog or Bluetooth output. Host execution time is not MIPS performance evidence. One documented limitation to investigate is effect activation halfway through a track: a 16-bit source can remain on the narrower processing path until the next track.

## Dependency updates and target evidence

Alpha10 uses LVGL 9.6.0, FreeType 2.14.3, Opus 1.6.1 fixed point, opusfile 0.12, Ogg 1.3.6 and WavPack 5.9.0. Vendored SQLite reports 3.53.4. Native version probes verified the linked libraries on R1. WavPack 5.9 produced the same PCM as 5.6, using 5.7% versus 5.4% of real time; Opus 1.6.1 used 9.0% versus 8.6% for 1.4 on the measured normal files. These are decoder workload samples, not whole-device CPU or audio-quality scores.

Official sources checked on 15 October 2026:

- [LVGL 9.6 changelog](https://lvgl.io/docs/open/changelog/CHANGELOG): the configured major/minor is already the 9-series stability release.
- [FreeType releases](https://freetype.org/index.html): alpha9's 2.15.0 changed small Segoe UI rendering; 2.14.3 was selected after the target comparison below.
- [Opus downloads](https://opus-codec.org/downloads/): 1.6.1 is built with fixed point and without deep PLC, DRED or OSCE for this device; opusfile remains 0.12.
- [WavPack downloads](https://www.wavpack.com/downloads.html): 5.9.0 is built without worker threads; native PCM/format checks pass.

The kernel remains `4.4.94-sonix` with the device's glibc 2.22 ABI. A kernel swap would require board drivers, suspend, audio and recovery validation; it is not a drop-in dependency update.

## Alpha9 typography regression

The two private Segoe UI font files and text-size preferences were unchanged. Using LVGL's actual FreeType load flags on R1, 2.15.0 changed 378 of 5,600 tested glyph records: Regular at 16, 17 and 18 pixels, including Cyrillic and digits. FreeType's local `docs/CHANGES` describes the changed v40 TrueType interpreter behavior and names Segoe UI. This explains why headings could match while durations and other small labels looked different.

FreeType 2.14.3 matched the former 2.13.3 metrics and bitmap fingerprints in all 5,600 records, covering both faces and 16 sizes. No font-size, weight, spacing or custom rendering workaround was introduced. Evidence: workspace `output/quality-review/font-{old,new,compat}.csv` and `font-differences.json`.

## First confirmed improvements

- The four UI corrections now pass targeted tests at both panel sizes and were checked in a temporary R1 candidate: joined shade, sticky album title/menu, track-owned playback colours, and metadata without the stale separator. Persistent status bar over the player has a separate regression check. Components with 16 quick controls and the common/custom-page regression also passed.
- The existing on-demand evdev reader was disabled by default. Actual target diagnostics attributed about 62 callbacks/second to `lv_indev_read_timer_cb` while untouched. Testing the same executable with an isolated `touch_events=1` configuration reduced idle loop frequency from roughly 72/s to 12/s and process CPU from 3.9% to 3.0% in comparable diagnostic windows. This is not a battery-life claim. Fresh first taps, scrolling, long press, screen-off/wake and edge-back were exercised. An initial wake test left a shade over a menu and was rejected; it was repeated from a clean list. Wi-Fi was restored after the stray test tap. R1 now enables this by default, with explicit `touch_events=0` fallback and unchanged defaults for unverified models.
- A stationary open shade drew approximately 1.5 frames/second, matching its 700 ms poll. Its refresh path set identical local colours and image sources every time. The correction avoids those redundant writes while retaining state polling. The native flush-count test passed at both panel sizes and confirmed a changed toggle still repaints. Hardware windows after settling show one small 2,340-pixel status redraw per 10 seconds instead of about 15 redraws averaging roughly 80,000 pixels. Evidence: `alpha9-efficient-perf.log`. The final candidate also started on-demand touch without a configuration override, verifying the R1 default.

Raw diagnostics are in workspace `output/quality-review/alpha9-{timed,events}-touch-perf.log`. Instrumentation is enabled in both compared runs. Startup windows and periods with navigation or Wi-Fi changes are excluded from idle comparisons. `SONIX_PERF` timer CPU numbers distribute handler cost across callbacks; they locate candidates but are not independent per-callback profiling.

## Build provenance correction

Static archives now install under versioned `freetype-target/<version>` and `audio-target/<version-set>` paths. Flag stamps invalidate consuming objects when versions/configuration change, including rollback. `tests/dependency_cache_test.py` verifies change, rollback and unchanged-build behavior. Downloaded archive hashes are checked before extraction. This fixes the earlier fixed-path cache issue; changing a version string alone was insufficient evidence.

## Release recommendation still withheld

The remaining acceptance items are final-image boot/standby/long-playback checks, the documented mid-track effect-path limitation, and physical output coverage. Live-account service coverage is incomplete: a Zvuk current-track save was verified in the alpha8 pass, but all authentication, download and network-transition states are not accepted. KA17 and analog measurements remain explicitly unverified. Short CPU samples and simulator passes cannot replace these checks.
