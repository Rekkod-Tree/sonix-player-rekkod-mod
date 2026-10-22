# Home and catalog navigation

## Home editor

Swiping a pinned shortcut left removes that shortcut, not its content. Undo
reinserts the same stable item ID at its former position in the current layout;
it does not replace later edits. An already restored item is not duplicated, and
a full Home never evicts another shortcut to make room for Undo.

The existing row recognizer now supports the editor's custom content. Its 72 px
cards, accent glyphs, typography and independent reorder grip are retained.
Vertical scrolling and edge Back remain separate gestures. The contextual removal
label wraps inside the revealed area; the moving card retains its own surface.

## Continue book

An ordinary tap resumes immediately from the saved checkpoint. It does not open
the action sheet, including when a book has been marked finished. A matching
paused book resumes; an already playing book continues. Missing card, file or
checkpoint reports an error instead of silently starting over.

Hold keeps the action sheet and adds a snapshot of chapter/part and time. A loaded
book uses its live position; an unloaded book uses the saved filename and time
without inventing an unknown chapter title. The sheet's Continue action belongs
to the selected book and card epoch, even if another selection changes later.
Home and More share the same ordinary-tap entrypoint.

## Yandex and Zvuk Back

Each existing eight-step history retains a compact stable item key and viewport
offset. An accepted response restores that anchor in one final list bind. Ordinary
Back tries the old index first; reordered data uses a local ID search. Missing
items use a clamped fallback. New categories, filters and queries start at the top.

Anchor lookup does not request metadata, artwork or disk data. A late response
cannot reset a different view's request marker. A different page in the same view
may deliver useful data without clearing the newer page request or its cooldown.
Playback queues and service playback controllers are unchanged.

## Verification and scope

Independent component and production-function fixtures passed normal and scoped
ASAN/UBSAN runs. Real LVGL lists cover 20,000 entries, both panel heights, grouped
headers, moved/missing IDs, viewport changes and one-bind restoration. Callback
tests cover overlapping contexts, generations and page numbers in both services.

The complete application was rendered in both themes at 480x800 and 480x720.
Real pointer tests cover swipe, Undo, reorder grip, vertical scrolling, global
edge Back and direct Home/More book taps. Long Russian titles and live/saved book
headers are readable. The shared Home integration fixture also checks a real
SQLite checkpoint after a finished-book tap; the existing playback/chapters
regressions passed on both panel sizes.

Native device, target ABI and image results belong in the final release manifest;
host fixtures alone do not establish live service or physical-panel acceptance.
Existing translations are reused in all 24 overlay files. No new background
worker, polling timer, library, font, palette or PGO training is added by this wave.
Queue refresh and download recovery are separate work packages.

## Cached playlist roles and streaming hints

The shared playlist screen restores ordinary browsing when an older history
entry is revisited after a picker. A synchronous unload-start marker avoids a
late unload event overwriting a fresh request. Naming cancellation stays in the
picker; an already captured queue-export job retains its own source and lifetime.
The actual GUI regression includes returning during a 20,000-row export, opening
the historical list and then verifying the worker's complete published result.

Seven existing streaming search fields use one shared opt-in placeholder color.
New palettes take the existing secondary-text role; legacy family0 removes that
override and retains the original default rendering. Login fields, network and
account behavior are unchanged. The fixture checks cached palette return, text,
cursor, geometry and font preservation on both panel heights.

Run from `sonix-player/`, with the normal host dependencies and R1 resources:

```sh
make host UI_TEST=1 -j4
SONIX_UI_TEST_PLAYLIST_ROLES=1 bash tests/run_graphite_ui.sh /tmp/sonix-role-check
make -f Makefile -f tests/stream_search_ui.mk UI_TEST=1 stream-search-ui-test -j4
SONIX_UI_TEST_BINARY="$PWD/sonix_player_host_stream_search_test" \
  bash tests/run_graphite_ui.sh /tmp/sonix-search-hint-check
```

These are host checks. Their release-specific native and PGO results belong in
the final firmware manifest; the legacy light-theme hint contrast is deliberately
not changed by this additive palette repair.
