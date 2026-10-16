# Streaming Audiobook Downloads

Implementation scope requested by the user: download audiobooks from Yandex Music
and Zvuk into the configured local **Audiobooks** library. Text ebooks are a
separate task. Podcasts must remain distinct from books.

## Confirmed examples

- [Yandex audiobook](https://api.music.yandex.net/albums/38681851/with-tracks):
  13 ordered parts, `type=audiobook`, `metaType=podcast`, `trackCount=0`,
  `pager.total=13`. Count the actual complete parts list, not `trackCount` alone.
- [Yandex podcast](https://api.music.yandex.net/albums/11002256/with-tracks):
  `type=podcast`, 299 episodes, newest first. Episodes carry publication dates
  and `type=podcast-episode`. The current album download limit is 150 tracks.
- User examples for Zvuk:
  [book](https://zvuk.com/abook/28850372),
  [podcast](https://zvuk.com/podcast/12874277).
  The public metadata request returned HTTP 418; its book/chapter API contract
  remains unverified. Current playback requests only the GraphQL `Track` type.

Anonymous Yandex availability does not establish the user's subscription rights.
No audiobook audio download or authenticated service test has been performed.
Service artist credits are not sufficient to distinguish author from narrator.

## Proposed implementation

Reuse the existing download workers, card ownership guards, tagging and cover
handling. Add an explicit audiobook destination to a complete-book job. Freeze
the configured `audiobookdb_folder()` and card generation when queuing it.

Use stable provider/book IDs for directory identity and deterministic ordered
chapter names. Validate complete paths against playback's 512-byte limit.
Publish a newly downloaded book from a hidden staging directory only after every
required chapter succeeds; then rescan Audiobooks. A partial download must not
silently become a complete book. Existing music downloads and saved-track
indexes must retain their original paths.

Reuse local audiobook resume: it already stores the book, chapter file and
position. Keep these identities stable across rescans and repeated downloads.
Do not delete an existing book before its replacement is ready. Cloud progress
synchronization is not included in the initial scope.

## Acceptance before release

- Confirm both supplied books' chapter count, order, title and artwork.
- Download into a custom nested audiobook folder and listen offline.
- Pause in a later chapter; resume after restart, rescan and repeated download.
- Preserve existing Music copies, bookmarks and completed status.
- Exercise cancellation, USB handoff, card removal, full card and unavailable
  chapters without publishing an incomplete book or resetting an existing one.
- Handle lists above current 150/300-entry limits explicitly; never truncate
  silently. Keep podcast episodes and their progress separate from books.

This document records the implementation boundary; audiobook service integration
is not implemented by the accompanying lazy-title and cover fixes.
