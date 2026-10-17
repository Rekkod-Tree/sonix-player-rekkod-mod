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
  and `type=podcast-episode`.
- Authenticated Zvuk examples:
  [book](https://zvuk.com/abook/28850372),
  [podcast](https://zvuk.com/podcast/12874277).
  The book has 145 chapters, numbered 1-145, and a duration of 53,246 seconds.
  The podcast returns 338 references, including two null entries: 336 valid
  episodes remain. Sort chapters by position and episodes by publication date;
  neither API array is ordered. Chapter/Episode streams are distinct from Track.

Authenticated metadata, search and typed Zvuk streams were checked on the R1.
Both Yandex examples report every part available on the saved session. Metadata
availability alone does not prove a complete audio download.
Service artist credits are not sufficient to distinguish author from narrator.

## Implementation

The existing workers use card ownership guards, tagging and cover handling.
A complete-book job freezes the configured `audiobookdb_folder()` and card
generation. The explicit limit is 300 chapters; larger books fail visibly.

Downloads use stable provider/book IDs for directory identity and deterministic ordered
chapter names. Validate complete paths against playback's 512-byte limit.
They publish a newly downloaded book from a hidden staging directory only after every
required chapter succeeds; then rescan Audiobooks. A partial download must not
silently become a complete book. Existing music downloads and saved-track
indexes must retain their original paths.

Local audiobook resume stores the book, chapter file and
position. Keep these identities stable across rescans and repeated downloads.
Do not delete an existing book before its replacement is ready. Cloud progress
synchronization is not included in the initial scope.

Podcast shows use album-style search rows and typed badges. Individual episodes
are saved into the existing `Podcast/` shelf with stable, separate Yandex/Zvuk
resume identities. There is no show-level archive download. Local listening
progress is not synchronized with a service's streamed playback progress.

Ordinary Music albums retain their existing bounded list parsing; only spoken
collections receive the new strict completeness checks. List images remain
disabled by default and do not add thumbnail requests or SD-cache writes.

## Validation boundary

- Confirm both supplied books' chapter count, order, title and artwork.
- Download into a custom nested audiobook folder and listen offline.
- Pause in a later chapter; resume after restart, rescan and repeated download.
- Preserve existing Music copies, bookmarks and completed status.
- Exercise cancellation, USB handoff, card removal, full card and unavailable
  chapters without publishing an incomplete book or resetting an existing one.
- Handle lists above current 150/300-entry limits explicitly; never truncate
  silently. Keep podcast episodes and their progress separate from books.

Native fixtures cover complete publication, failed/missing parts, repeat saves,
card generation changes, namespaces, real SQLite rescans and resume. Native UI
checks use real HTTP download workers and decode the published audio. Hardware
acceptance must be reported separately from these fixtures. Live USB handoff
and the intermittent mass-storage disconnect are deferred at the user's request.
