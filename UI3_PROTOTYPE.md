# Graphite 2 reference prototype

The approved interactive reference is in `prototype/graphite2/`. From the repository root, run `python3 -m http.server 8765`, then open `http://127.0.0.1:8765/prototype/graphite2/`.

The prototype uses bundled React, SVG artwork and the system Segoe UI font with fallbacks. No private font files, account credentials or real service sessions are included. Service responses and track names are demonstrations; firmware behavior must be checked separately.

The current reference includes the neutral compact status bar without local transport/sleep icons, mini-player colours on the control centre and volume overlay, and flat playback surfaces. Native-only features retain their controls and use the same visual system. Device sizes remain 480x800 and 480x720.

`UI3_COVERAGE.md` records hardware and native-runner acceptance limits. `UI3_VALIDATION.md` records build evidence. `BOOT_BRANDING.md` describes the separately rendered startup artwork.
