# Release quality review

Work in progress, 15 October 2026. Baseline: `main` at `6ea05b8`, installed R1 alpha8 (`141020262333`). This review covers the full requested scope: reliability, responsiveness, energy, end-to-end audio, architecture/kernel and dependency versions. A completed UI pass does not close the release review.

## Evidence and acceptance still required

| Area | Current evidence | Work remaining before a release recommendation |
|---|---|---|
| UI fidelity | Four alpha8 findings reproduced on R1; corrections in progress. Targeted native tests cover the album header/menu, independent playback palette and joined shade at both panel sizes. | Final regression and physical candidate verification, including persistent status bar and selection-mode transitions. |
| Reliability | Host/MIPS builds and glibc 2.22 ABI check pass before the latest status-bar extension. | Review thread ownership, cancellation, storage transitions, decoder/download errors; run fault and repeat-transition checks for confirmed risks. |
| Responsiveness | Existing eventfd/poll wake path and built-in `SONIX_PERF` counters found. Baseline process/thread samples captured. | Real frame/timer measurements during navigation, scrolling, cover loading and playback; compare any optimization against baseline. |
| Energy | Two 30-second R1 idle samples with screen on/off; no RSS growth within either window. | Playback and standby samples, identify avoidable wakeups, repeat comparable measurements. USB-powered readings cannot prove battery life. |
| Audio | Existing resample/interpolation/Bluetooth-policy tests pass. Source has wide processing for enabled effects and final S16 TPDF dither. | Verify test coverage, bypass and gain/clipping behavior, live effect changes, route negotiation, actual DAC/Bluetooth/USB delivery and underrun behavior. Analog THD/noise needs a suitable measurement path; software checks do not establish it. |
| Architecture/kernel/dependencies | Actual kernel `4.4.94-sonix`, single XBurst MIPS32r2 CPU, 58,560 KiB RAM, zram swap. Target loads OpenSSL 1.1 libraries; host TLS tests use a different version. | Complete version/provenance inventory, assess upstream fixes and ABI/resource tradeoffs, validate selected updates on target. |

## Initial device baseline

Evidence: workspace `output/quality-review/alpha8-idle-screen-{on,off}.txt` and `baseline-summary.json`. Each sample spans approximately 30 seconds. Counters are process ticks divided by total CPU ticks, not a profiler estimate or battery measurement.

| State | Player CPU | System busy | RSS start/end | Main-thread voluntary switches |
|---|---:|---:|---:|---:|
| Paused, screen on, brightness 63 | 3.341% | 5.479% | 21,224 / 21,224 KiB | 2,729 |
| Paused, screen off | 1.165% | 2.097% | 21,244 / 21,244 KiB | 342 |

The main/UI thread and USB watcher account for most sampled work. The stock `axp_battery` interface reports zero voltage/capacity while the separate battery interface reports 4.392 V and 100%; USB power is connected. Do not infer discharge current or runtime from these inconsistent interfaces. The screen was restored to brightness 63 after the measurement.

## Existing audio checks

`output/quality-review/run_dsp_checks.py` compiles and runs repository tests. `resample-test.log`, `interp2-test.log` and `btrate-test.log` record passes. These test digital filters and policy; they do not capture the final analog or Bluetooth output. Host execution time is not MIPS performance evidence. One documented limitation to investigate is effect activation halfway through a track: a 16-bit source can remain on the narrower processing path until the next track.

## Dependency review opened

Versions in the build: LVGL 9.6.0, FreeType 2.13.3, Opus 1.4, opusfile 0.12, Ogg 1.3.5, WavPack 5.6.0; vendored SQLite reports 3.53.4. `tools/system-libs/build.sh` lists additional pinned libraries, but installed binaries must be checked separately.

Official sources checked on 15 October 2026:

- [LVGL 9.6 changelog](https://lvgl.io/docs/open/changelog/CHANGELOG): the configured major/minor is already the 9-series stability release.
- [FreeType releases](https://freetype.org/index.html): 2.15.0 was released on 12 October, with rendering changes relevant to Segoe UI; earlier 2.14 maintenance releases include security fixes. Candidate upgrade and rendering/resource comparison remain pending.
- [Opus downloads](https://opus-codec.org/downloads/): libopus 1.6.1 is newer than the pinned 1.4; opusfile 0.12 remains listed. MIPS decoder configuration and CPU cost must be checked before an upgrade.
- [WavPack downloads](https://www.wavpack.com/downloads.html): 5.9.0 is newer than the pinned 5.6.0. Decoder changes and test coverage still need review.

No blanket dependency or kernel upgrade has been applied. Remaining work is substantive, not just packaging or documentation.

## First confirmed improvements

- The four UI corrections now pass targeted tests at both panel sizes and were checked in a temporary R1 candidate: joined shade, sticky album title/menu, track-owned playback colours, and metadata without the stale separator. Persistent status bar over the player has a separate regression check. Components with 16 quick controls and the common/custom-page regression also passed.
- The existing on-demand evdev reader was disabled by default. Actual target diagnostics attributed about 62 callbacks/second to `lv_indev_read_timer_cb` while untouched. Testing the same executable with an isolated `touch_events=1` configuration reduced idle loop frequency from roughly 72/s to 12/s and process CPU from 3.9% to 3.0% in comparable diagnostic windows. This is not a battery-life claim. Fresh first taps, scrolling, long press, screen-off/wake and edge-back were exercised. An initial wake test left a shade over a menu and was rejected; it was repeated from a clean list. Wi-Fi was restored after the stray test tap. The R1 default is being enabled, with explicit `touch_events=0` fallback and unchanged defaults for unverified models.
- A stationary open shade drew approximately 1.5 frames/second, matching its 700 ms poll. Its refresh path set identical local colours and image sources every time. The correction avoids those redundant writes while retaining state polling. The native flush-count test passed at both panel sizes and confirmed a changed toggle still repaints. Hardware windows after settling show one small 2,340-pixel status redraw per 10 seconds instead of about 15 redraws averaging roughly 80,000 pixels. Evidence: `alpha9-efficient-perf.log`. The final candidate also started on-demand touch without a configuration override, verifying the R1 default.

Raw diagnostics are in workspace `output/quality-review/alpha9-{timed,events}-touch-perf.log`. Instrumentation is enabled in both compared runs. Startup windows and periods with navigation or Wi-Fi changes are excluded from idle comparisons. `SONIX_PERF` timer CPU numbers distribute handler cost across callbacks; they locate candidates but are not independent per-callback profiling.

## Build provenance finding to resolve before library updates

The static library rules in `sonix-player/Makefile` install to fixed `freetype-target` and `audio-target` paths, and their `.a` targets do not depend on the version variables. Changing `FREETYPE_VER` or an audio library version can therefore leave the old archive in place. Versioned build/install paths or equivalent explicit invalidation are needed before testing upgrades; merely changing a version string would not prove the new library is linked.
