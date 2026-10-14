# Release quality review

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
