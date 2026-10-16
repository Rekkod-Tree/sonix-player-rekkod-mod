# Graphite 2 native UI

## Alpha12 compact search and history

Search now uses one 52 px horizontal category row: All, Tracks, Albums and Artists. It stays above the result scroller and hides while typing. Categories with no hits remain selectable. Returning from the keyboard or replaying history reveals the selected tab, without overriding subsequent manual scrolling.

Recent queries are compact rows with independent remove buttons. Repeating a query restores its category; another submission moves it to the front without duplication. Leading/trailing whitespace is trimmed, blank submissions keep the keyboard and history open, and removing an entry does not execute it. The six-entry history retains the existing 127-byte configuration-value limit: longer queries can still search, but are not saved as misleading truncated entries.

Native regression coverage checks category-row geometry, results above the keyboard, restored Albums/Artists selection, empty Albums followed by Tracks, normalization and real-pointer deletion. An initial test expected the full album title despite the shared row's ellipsis; its lookup now uses a stable prefix and its result-container lookup walks to the screen's direct child. Screenshot inspection confirmed the album itself was correctly present. Final integrated evidence and hardware limitations are recorded in `RELEASE_QUALITY_REVIEW.md`.

## Alpha11 search and keyboard corrections

The search overlap was reproduced in the native simulator with the alpha10 implementation: `search-results.png` in workspace `output/native-ui-search-before/800/` shows track rows over the filter card, and the new geometry assertion failed. Opening/closing the keyboard moved group headings but left already-bound row objects at their old positions. Search now measures the visible filter card before positioning groups and moves retained rows with their group. Format badges also use the shared metadata layout without a leading separator.

The Recent heading had a separate clipping defect: a fixed one-line label received 12 pixels of internal top padding. That spacing is now a margin. The regression checks results below filters, two rows above the keyboard, Recent heading content height, and replaying a query from history in the other theme. The history test initially found a hidden subtitle from the old results; its lookup is now scoped to the Recent container.

Independent review also found that T9 kept its pending character when switching input fields. The focused regression failed on the old implementation, then passed with commit-on-field-change; selecting the same field still permits cycling through the key's letters. These are native UI checks, not physical-device acceptance of alpha11. The user postponed the real-device screenshot sheets, and the connected R1 remains on alpha10.

The integrated alpha11 host/MIPS build and glibc 2.22 ABI check pass without compiler warnings. The core UI run passed at both 480x800 and 480x720 (50 captures each), including search reflow and Recent checks; the separate T9 field-switch run also passed at both sizes. Evidence: workspace `output/native-ui-alpha11-core/` and `output/native-ui-alpha11-keyboard/`. The service-cache AddressSanitizer fixtures and 11 logging rotation/storage/USB scenarios passed. Independent review of the final production changes has no blocking findings; remaining limitations are recorded in `RELEASE_QUALITY_REVIEW.md`.

## Alpha10 typography compatibility

The installed alpha9 used the same private Segoe UI files and saved text sizes as alpha8. The difference was FreeType 2.15.0 rasterization of Regular at 16-18 px: a target-run comparison found 378 changed glyphs. FreeType 2.14.3 restores the previous metrics and bitmap fingerprints in all 5,600 checked glyph records (two faces, 16 sizes, Latin/Cyrillic/digits/punctuation). The target archive was rebuilt and the comparison repeated against the final archive, again with zero differences. Font weights, sizes and layout were not adjusted.

The common native regression passed at 480x800 and 480x720 with private prototype fonts. Version-cache change/rollback checks and the glibc 2.22 ABI check passed. The rebuild has no new warnings; the pre-existing upstream SQLite `strlen` diagnostic also appears in `output/sonix-baseline-build.log`.

The final executable ran from an isolated SD directory on the physical R1. Captures `alpha10-home.png`, `alpha10-music-grid.png`, `alpha10-track-list.png`, `alpha10-music.png` (full player), `alpha10-settings.png` and `alpha10-wake.png` cover the typography and page geometry. Normal 24-bit/48 kHz playback reported zero ALSA recoveries in sampled windows, and a short screen-off/wake cycle returned to the settings page. Preferences were read back unchanged. Bluetooth did not regain its A2DP profile during this temporary restart, so this pass does not establish Bluetooth reconnect acceptance.

Evidence is in workspace `output/native-ui-alpha10-core/`, `output/device-audit/alpha10-*.png` and `output/quality-review/{font-compat-final.csv,alpha10-device.log}`. This verifies the candidate executable on R1; the packaged alpha10 image still requires installation and boot. KA17 output, long standby/playback and full live-account coverage remain open in `RELEASE_QUALITY_REVIEW.md`.

## Post-alpha8 fixes and measured idle work

The candidate corrects A8-01 through A8-04: the shade joins the status bar without upper corner cutouts, the scrolled album retains its title/menu, playback colours follow the playing track independently of the browsed album, and right-aligned metadata no longer starts with a separator. This includes the optional persistent status bar over the flat player. No new firmware image is claimed here.

`output/native-ui-alpha9-refinements-final/`, `native-ui-alpha9-components/`, `native-ui-alpha9-regression/` and `native-ui-alpha9-idle-panel/` passed at both panel sizes. The last check counts real flush events: a settled shade does not draw unchanged controls, while toggling MSEB causes a visible repaint. The prototype metadata layout was checked in Chromium with no JavaScript errors. R1 captures `alpha9-shade-joined.png`, `alpha9-album-sticky.png`, `alpha9-album-sticky-menu.png`, `alpha9-player-over-album.png` and `alpha9-events-scroll.png` verify the corrected device states.

Existing on-demand touch reading was verified on R1 with first taps, scrolling, long press, screen-off/wake and an edge-back gesture; it is now the R1 default, preserving explicit configuration and the old default on other models. The final temporary candidate started in on-demand mode without a configuration override. With diagnostics enabled in both runs, settled paused-player windows dropped from about 72 loop passes/s and 3.9% process CPU to 12/s and 3.0%. Shade polling still checks live state, but avoids unchanged style/source writes: hardware windows dropped from roughly 15 large redraws per 10 seconds to one small status-region redraw. These are workload measurements, not battery-life or end-to-end touch-latency claims.

The wider reliability, audio, energy and dependency review remains active in `RELEASE_QUALITY_REVIEW.md`.

## Alpha8 component and playback-menu pass

The current work is on `main`, following gray-accent commit `f04be22`. Filled choices, title/switch alignment, full-width notes, named accents, two-line quick captions and shared date/time actions address the component review. The user explicitly retained the existing 64x300 volume pill; the prototype now follows it. The status bar adopts the open shade's palette and restores its page/artwork state on dismissal. List rows reserve 12px on the right and preserve complete format/duration text before truncating a long artist name.

The current-track menu supports permanent saves through the existing Zvuk and Yandex download workers. Tests cover the original track after playback changes while the menu is open, already-saved state, a missing card and local-track exclusion. No download action is invented for providers without a permanent-download facility.

Host and MIPS builds pass without new warnings; the ABI check remains within glibc 2.22. Both panel sizes passed the component, row-bounds and status-bar checks. Earlier in this pass, the 36-page dark/light regression and streaming-save fixtures passed. Evidence is under workspace `output/native-ui-alpha8-{full,save,rows-final,status-final}/`. An initial row-test cleanup deleted its screen during a transition; that test defect was fixed and the affected scenarios rerun. Screenshot inspection additionally exposed duration clipping beyond its parent; the layout and the containment assertion were corrected together.

On the physical R1, temporary candidate binaries were launched from an isolated SD directory while preserving the installed alpha7 root filesystem. Fresh ADB captures confirmed dark/light choices, Bluetooth summary/details without changing the battery toggle, date/time cancellation, the shade/status-bar palette and live Zvuk menu states. Saving one live track from its playing menu completed and changed the item to Saved. The final packaged executable is also used for the row-spacing hardware check. This is targeted hardware evidence, not acceptance of every service, Bluetooth codec or storage failure. Yandex live-account testing and R3 Pro II hardware testing remain outstanding.

The final common regression, including list gestures and queue behavior, also passed on both panels in `output/native-ui-alpha8-core-final/`. The R1 was returned to its installed executable and running launcher after the audit; temporary SD candidates and private rollback copies were removed. Gray/dark/tint/text/timeout/brightness/HFP preferences were read back unchanged. The single downloaded test track was retained on the card.

The local R1 image is `D:/Projects/Mediateka/firmware/1.3.0-ui3-alpha8/r1.upt`, SHA-256 `259ed97d014423e72972b98d8cab558bc275dd5c38f67e926903d1cf938a96cf`. Unpacked binaries, R1 kernel, chunk chains, startup JPEGs, private fonts, sealed keys and translations match the intended inputs. Active-source highlighting on Home remains an unimplemented design proposal.

## Earlier implementation history

Implementation on `codex/graphite-ui3`, based on `sonix-r1` commit
`ab468170f146670a7d8109676ed98a4a96ddbcf8`. Build evidence and remaining hardware acceptance are distinguished below.

## Implemented

- Shared Graphite dark/light colours, readable accent foregrounds, consistent spacing, tiles and settings cards. Explicitly saved accent/tint choices remain intact.
- An Accent mini-player with track/artist information; square artwork and a compact, right-aligned Up Next control in the standard player. Audio-chain details open from the menu independently of the summary-line setting.
- Left swipe toggles a track's favourite state; right swipe queues it next. Album append, queue removal, selection and reorder controls keep their separate behavior.
- Stable identity-based artwork placeholders across library, search, folders, queue and playback; distinct artist and category treatments. Missing-art glyphs give way to the playing indicator.
- Search hides filter controls while typing, retaining two normal-sized track rows above the keyboard at 480x720. Long sheet descriptions scroll above their actions. Two-line Undo toasts pause their timeout while touched.
- Existing Waveform, Studio, lyrics and service features remain available. No production polling timers or rendering dependencies were added.

## Verification

Ubuntu 26.04.1 under WSL: host and MIPS builds completed. Final compilation has no warnings. `check-abi` reports no symbol beyond glibc 2.22.

The opt-in native LVGL runner passed with R1 and R3 Pro II resource overlays at 480x800 and 480x720. It captures 32 states per device and asserts swipe dispatch, selection behavior, queue removal, retained preferences, Up Next bounds, search-row visibility, long-message action bounds and Undo after a prolonged press. Zvuk uses the repository's local mock account; fixtures contain silent FLAC files and generated cover art.

```sh
cd sonix-player
make host UI_TEST=1 -j4
sudo bash tests/run_graphite_ui.sh /tmp/sonix-ui-evidence
```

Besides build dependencies, the runner needs Python Pillow, `flac`, `metaflac`, and mount namespaces. It uses disposable configuration/media roots and an exclusive localhost mock port, 18766. The test runner is excluded from the device binary.

## Local R1 image

`D:/Projects/Mediateka/firmware/1.3.0-ui3-alpha1/r1.upt` is an alpha image for HiBy R1. Its unpacked player/launcher match the final build; kernel, chunk MD5 chains, image sizes, translations, sealed service keys and Linux script endings were checked. No plaintext service keys are included. Details and SHA-256 are beside the image in `manifest.json` and `SHA256SUMS`.

Native screenshots and logs are in `D:/Projects/Mediateka/output/native-ui-final-acceptance/`.

## Remaining acceptance

The user installed alpha1 and then alpha2 on an R1. ADB inspection confirmed real visual mismatches in alpha1: MiSans typography, tile content alignment and missing counts, different menu icons, player geometry and the old album hero. Alpha2's running process maps both Segoe UI faces. Alpha3 corrects the listed geometry/components in the working build, with prototype comparisons and native tests. These are not a claim of complete hardware/state acceptance; see `UI3_COVERAGE.md`.

Physical touch comfort, device rendering/audio performance, standby, Bluetooth/LDAC, storage failure handling and a live Zvuk account still require hardware testing. The R3 Pro II interface was tested in the simulator; no R3 firmware image was packaged.

## Prototype typography correction

The prototype uses Segoe UI. Browser inspection resolves the tile's CSS weight 500 to Segoe UI Semibold. Optional `ui-regular.ttf` and `ui-semibold.ttf` now lead the shared native font chains; existing MiSans and script-specific fallbacks remain available. The page heading and player title use 32px Semibold, and player artist lines use 22px Regular.

The two Windows font files are private build inputs in `D:/Projects/Mediateka/local-assets/fonts/`, outside this source checkout. They must accompany the executable in `/usr/resource/sonix/fonts/`; rebuilding the executable alone does not install the faces. The local packaging helper copies them. They are included in alpha2 and alpha3.

Run the native tests with `SONIX_UI_FONTS_DIR` pointing to this directory. They verify Cyrillic resolution from both prototype faces and CJK fallback, and exercise both 480x800 and 480x720. The final typography run passed for both sizes; evidence is in `D:/Projects/Mediateka/output/native-ui-segoe-final/`. Host and target builds have no warnings; the ABI check passed. Rendering on the physical player remains unverified for the new font build.

## Alpha3 fidelity pass

The corrected R1 image is `D:/Projects/Mediateka/firmware/1.3.0-ui3-alpha3/r1.upt`; its manifest and checksum identify the final binary. Tile dimensions/baselines/icon sizes are asserted, descriptions refresh on library update events, and the approved SVG forms are rasterized by the existing generator. Player/mini-player/album geometry and shared navigation/settings components were adjusted without changing playback, queue or service APIs.

Extended evidence is in `output/native-ui-fidelity-final/` under the workspace: 98 native captures per panel, including 31 additional pages in both themes. Large-text findings from this run were repaired and the affected core, alternate-layout, sheet, search, compact-album and large-text scenarios repeated in `output/native-ui-alpha3-release/`. Complete hardware, live-service and custom-screen acceptance remains open in the coverage checklist.

## Alpha4 hardware-informed pass

Alpha3 was inspected on the real R1. Real album artwork had square corners and scrolled track text could overlap Back. These are corrected in alpha4, alongside service tile glyphs, carousel rows/placeholders/grip, file-manager rows, ebook placeholders/captions and shared game-setting choices.

`output/native-ui-alpha4-custom-release/` passed on both panels with 22 captures each. The final common regression in `output/native-ui-alpha4-accepted/` passed on both panels with 43 captures each, including actual two-colour fixture artwork, scrolling-header pixel checks, carousel dismissal, EPUB rendering and first boot. The tests resolve canonical album identity and verify hidden overlays; earlier placeholder/overlay captures were not accepted as proof.

The R1 image is `D:/Projects/Mediateka/firmware/1.3.0-ui3-alpha4/r1.upt`, SHA256 `1e72c771622c34e267af69acbde4a4f4ae1237d9e26831932e52547e15e67053`. It was unpacked and checked against the final target build. Subsequent hardware inspection confirmed rounded real artwork, an opaque scrolling header and carousel behavior. Live Zvuk remains outstanding; the user will sign in after alpha5 installation.

## Alpha5 hardware-informed pass

The connected R1 reported alpha4, build `141020262014`. Captures under `output/device-audit/alpha4-*.png` informed the control-centre and naming-form changes. Preferences and timeout were restored and read back after the audit; temporary ADB helpers were removed.

The alpha5 common regression passed on both panels in `output/native-ui-alpha5-core/`. Final targeted forms and service runs are recorded separately in the release manifest. They include real simulated pointer input for caption hold and long-error scrolling, cancellation without creating data, return after sign-in, Retry and action bounds above the mini-player. Service responses are local fixtures. No new production polling timer was added.

Build and image evidence is recorded beside `D:/Projects/Mediateka/firmware/1.3.0-ui3-alpha5/r1.upt`. Complete physical-device and live-account acceptance is still required; see `UI3_COVERAGE.md`.

## Alpha6 integrated release

The final R1 image includes the compact neutral status bar, control/volume surfaces in the mini-player colours, flat standard playback canvas, atmospheric Rekkod Mod boot artwork and the Zvuk profile-redirect sign-in correction. The interactive reference is included under `prototype/graphite2/`.

Status checks passed at both panel sizes with all clock positions, 12-hour time, three-digit volume, large text and artwork/theme transitions. Surface checks passed in both themes and with cover tint. The service mock confirmed sign-in through both 307 and 308 profile redirects and rejection of an invalid token. The final common regression is recorded in `output/native-ui-alpha6-release-core/`; detailed outcomes accompany the local firmware manifest.

Host and MIPS builds completed without new warnings; ABI remains within glibc 2.22. The final image was unpacked and checked against the target executable, launcher, R1 kernel, translations, private font inputs and sealed service keys. All three startup JPEGs match the source overlay, are RGB/non-progressive and 480x800. R3 Pro II source assets are 480x720; no R3 image is supplied.

Image SHA-256: `d2aa54b1333a7f49e7c7485f080f6541ae4c9b18c0ced2033d1ab2702fce64d7`. This identifies the complete alpha6, replacing earlier undelivered alpha6 candidates. Installation, boot rendering and live Zvuk session persistence on alpha6 are still pending.
