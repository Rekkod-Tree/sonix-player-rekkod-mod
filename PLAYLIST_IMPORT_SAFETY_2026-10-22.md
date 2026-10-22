# Atomic playlist import and writer safety

Playlist imports now build the replacement in the existing hidden stage and publish it in one final transaction. The previous playlist survives parse, allocation, row-write, rename and final-commit failures. Import no longer drops a public name ending in `(import)` or falls back to saving under that different name. Input M3U files and backups are unchanged.

The existing GUI export bound stays 16 rows. Import and one-time marked-M3U migration use one 256-row heap buffer, resolving metadata between short transactions. The stage captures the index epoch and playlist namespace revision before parsing: a changed card/index, edited target or another playlist edit rejects publication. A scan-owned transaction causes a safe failure, not writes inside that transaction. A stale job cannot discard a new job's stage. SQLite identifier existence uses ASCII NOCASE consistently with SQLite names; CREATE cannot silently append to an existing case alias. Lookup errors are distinct from an absent identifier, so bind/step failure cannot turn CREATE into an accidental append. User title collation is unchanged.

The legacy append writer rejects a foreign transaction before CREATE or statement preparation, checks reset/bind/step/commit outcomes, and bumps the generation only after success. Its live statement is revoked at both connection-close paths, allowing a late caller to finish without touching a newly opened index. Prepared append_all now uses the same bounded stage, including its existing zero-row create/success contract; no production caller retains a low-level writer between row calls. It is a compatibility API, not a connection-wide transaction arbiter; production metadata-reading import/migration no longer holds that long transaction. No ownership hooks or broad database rewrite were introduced.

Migration distinguishes error (-1), valid empty (0), and successful row count. Read/header/directory errors do not mark migration complete. The current session still attempts it only once; the next mount/start can retry, without a polling loop. Completion is epoch-bound and refuses a foreign transaction. Successfully migrated files stay as backups.

Cost is explicit: on a disposable on-disk host database, 20,000-row replacement changed from 4 successful total commits to 81 (79 row batches, stage creation and publication). SQLite cache-page writes were 324 versus 556. Final three host runs: baseline 109–112 ms; candidate 349–350 ms. These are host results, not SD timing, physical NAND writes or battery measurements. Peak row-buffer storage is 462,848 bytes on the 64-bit host, about 450 KiB with the target's 32-bit long. Root must check real-card latency and available memory before release. No permanent worker, timer, new queue or file-format change is added. One-time legacy migration remains synchronous as before, so its first-run cost also needs hardware acceptance.

## Verification

Standalone ordinary fixtures exercise the actual library/import bodies and SQLite implementation: target row metadata/generation/order, 20k entries, partial-read ferror, explicit allocation/SQL return-code failures, rename/final-commit rollback, foreign transaction and metadata/backfill overlap, namespace changes, card/index epoch changes, case aliases/quoted names, existing append and migration callers. Normal O3 and ASan/UBSan/LSan runs pass. The changed translation units are instrumented; the unchanged production SQLite object reused by the tests is not rebuilt with sanitizers. No OS memory limits or special SQLite-memory experiment is involved.

From `sonix-player/`, with the host SQLite object available:

```sh
cc -std=gnu11 -O1 -g -DHOST_BUILD -DLV_CONF_INCLUDE_SIMPLE -I. -Ilvgl \
  -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  tests/playlist_atomic_core_test.c build_host/system/db/sqlite3_impl.o \
  -Wl,--gc-sections -pthread -lm -o /tmp/playlist-core-test
/tmp/playlist-core-test
```

The import harness additionally links `tests/playlist_import_atomic_fixture.c` and `src/system/core/utils.c`. Hardware/ABI/full firmware validation and merge remain separate gates.
