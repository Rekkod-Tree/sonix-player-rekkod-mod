# Graphite 2 coverage and acceptance

The approved prototype remains the visual reference. Native features absent from it keep their behavior and use the shared typography, colours and controls. A successful build or screenshot count does not establish complete acceptance.

## Implemented in the alpha3 work

| Area | Change and verification scope |
| --- | --- |
| Typography | Segoe UI Regular/Semibold, script fallbacks. Both faces confirmed mapped by the running R1 on alpha2. |
| Tile pages | Shared dimensions, 34px icons, bottom-aligned titles, reserved descriptions and live library counts. Geometry assertion and screenshots at both panel sizes. |
| Track lists | Shared title/subtitle scale, identity colours and unchanged swipe/selection/queue handlers. Native interaction checks. |
| Mini-player | 64px artwork, consistent margins, full-width progress and existing gestures. Native screenshots. |
| Standard player | Prototype panel insets, title/artist scale, seek geometry, transport position and Up Next grouping. Pressed state, chain details, artwork and layout switching exercised. |
| Local album | Horizontal 156px artwork, metadata and actions; compact preference retained. Large text remeasures metadata and track positions. |
| Settings | Shared card spacing, grouped navigation lists and larger setting choices. Compact service/search bars retain their own sizing contract. |
| Service UI | Shared rows, headers, cards, sheets and error presentation; mock Zvuk login/catalog/album exercised. No claim of live-account acceptance. |

## Native evidence

`D:/Projects/Mediateka/output/native-ui-fidelity-final/` contains the extended run at 480x800 and 480x720: core scenarios plus 31 pages in dark/light themes, followed by large-text and compact-album cases. The large-text run exposed wrapping defects in the four-way choice and album metadata; these required a corrective pass. Later evidence and image provenance are recorded in the release manifest and local checkpoint.

The additional pages include Bluetooth and its audio settings, EQ, MSEB, DAC filters, fades, playback options, volume limit, main/Music/settings/appearance, power/system/languages/button mapping/control-centre settings/developer options, networks/Wi-Fi/transfer, audiobooks/playlists/folders/More, streaming and service login pages.

## Still required for full acceptance

- Install the final alpha6 on the R1 and compare the new status bar, matched control/volume surfaces, flat player and startup artwork. Alpha5 was inspected on the connected R1; its populated status bar exposed the clock/moon overlap addressed here.
- Complete physical Bluetooth/audio and card removal/USB-export transitions. On alpha3, both track swipes were exercised and reversed, and playback position advanced across screen-off/wake.
- Verify live Zvuk account, download and error/recovery states; current service tests use disposable local fixtures.
- Complete remaining service-specific dialogs and game-runtime states. The carousel and its track list, ebook shelf/reader/settings, game library/settings, file manager and first-boot greeting have now been inspected in the native runner; this does not verify every runtime state.

R3 Pro II verification is simulator-only; no R3 image has been packaged.

## Alpha5 verification scope

Alpha4 was checked on the connected R1, including the real album, carousel, player/menu, custom Music tile order, control centre and playlist naming/cancellation. No playlist was created. Original accent, tint and 30-second screen timeout were restored; volume stayed at the user's 57.

Alpha5 aligns the control centre with the prototype, unifies playlist/folder name fields and action corners, groups Zvuk navigation with header Search and real known counts, and replaces unbounded service errors with a scrolling explanation and fixed actions. Failed empty loads hide album/artist/search/wave bars. Sign-in offers the existing import flow, and returning after authentication retries the failed list.

Native evidence is in `output/native-ui-alpha5-core/`, `output/native-ui-alpha5-forms-final/` and `output/native-ui-alpha5-services-final/`. The targeted final runs exercise caption hold, fast panel dismissal, form cancellation, expired sessions, return after login, Retry, subscription/failure states and actual touch scrolling of a long message at large text size. These use disposable fixtures and a local service mock, not the user's account.

Alpha5 control-centre appearance was inspected on hardware. Live Zvuk import was blocked by repeated profile redirects; the source correction uses the existing GraphQL validation fallback and retains rejection of invalid tokens. Live import, downloads and session acceptance after installing the correction remain pending. Bluetooth/audio measurements, storage transitions and unvisited service/game states remain open.

## Alpha6 integrated pass

The status bar follows the prototype's 20px type/glyphs and 24px battery. Normal states share neutral ink; volume and low-charge warnings retain semantic colours. Sleep and local transport indicators are removed while their functions remain. Native tests cover densely populated indicators, both clock formats, all positions, large text, light/dark colours and artwork transitions.

Control centre and volume overlay use the mini-player's actual background/foreground, including pale cover accents. Selected quick controls reverse those colours. High-volume text uses a contrast-preserving warning badge. The standard player has a flat page-coloured background even when cover tint is enabled; native Studio and Waveform modes remain available.

`output/native-ui-alpha6-surfaces-final/` checks the three surfaces together in both themes with and without cover tint. `output/native-ui-alpha6-status-12h/` checks status layout. `output/native-ui-alpha6-integrated-services/` checks 307/308 profile fallback and invalid-token rejection against the local mock. These do not replace hardware acceptance.

The atmospheric boot image is fitted to both panels and encoded as baseline JPEG in all three existing slots. Its image dimensions and encoding are verified; actual startup rendering awaits installation. The source artwork and reproducible conversion are documented in `BOOT_BRANDING.md`.

## Alpha4 findings and corrections

Real R1 captures confirmed square loaded album artwork and track text scrolling beneath the back arrow. Alpha4 uses an independent rounded cover frame and an opaque header layer. The carousel now shares identity-based placeholder colours and 88px track rows; a 72px grip retains a fast drag. Releasing before the next frame exposed stale coordinates: committing layout before the close decision fixes this reproducible case.

Service tile glyphs are taken from the prototype. Source badges are retained. Game settings reuse the shared choice component, including wrapping based on label length. File-manager rows and ebook placeholders/captions use the common visual scale; ebook reading preferences remain independent.

Verification was strengthened after two false positives: a screenshot of a following page could still be covered by the carousel, and a raw album title could retrieve tracks while missing the cover cache's canonical album identity. Tests now assert that the carousel is hidden and resolve album identity through the normal library API; the artwork check requires the actual fixture's two image colours, not just a non-empty placeholder.
