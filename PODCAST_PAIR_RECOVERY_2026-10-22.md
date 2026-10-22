# Podcast audio and metadata recovery

Issue #30 extends the card-lifetime isolation in #29. Replacing an existing RSS
episode must not leave new audio paired with old metadata when publication fails.

## Publication and recovery

- Short admitted directory operations preserve the previous pair in owned hidden
  backups. A bounded per-folder record identifies the intended files and hashes.
  New audio is hashed while downloading; old audio is hashed only on overwrite,
  in admitted chunks no larger than 32 KiB.
- Cancel or a normal publication error restores the complete previous pair.
  A remaining record makes the affected download unavailable until checked by the
  existing worker. An unknown record or conflicting file preserves the evidence;
  it does not authorize deleting or replacing arbitrary files.
- One RAM reservation serializes RSS publication/recovery without retaining card
  descriptors during network waits. Recovery checks the card epoch and folder
  identity at each admission. Normal markerless shelf entries start no new worker.
- A matching native episode ID is required when existing metadata contains one.
  A different episode with the same filename is reported as a conflict. Legacy
  manual files without this metadata remain compatible.

This is a file-pair recovery record, not a persistent download queue. Artwork is
optional. No new library, permanent worker or periodic recovery timer is added.

## Verification and limits

Independent review accepted the exact v2 implementation after reproducing and
repairing readiness races. Tests use real publication code, deterministic syscall
barriers, sanitizer checks and 36 process-interruption points. Unknown incomplete
records deliberately remain unresolved rather than claiming universal recovery.

On the real R1, six static MIPS runs passed: publication/Cancel, readiness barriers
and multi-folder recovery on both RAM storage and an owned FAT directory. These
exercise actual filesystem calls, including reopened directory enumeration;
HTTP and storage-manager admission are mocked. All 35 synthetic files were
archived, hash-verified and removed. The user's card was not removed or forcibly
unmounted. Process interruption is not proof of atomic physical power-loss safety.

The integrated host Home suite passed on both panel sizes. Target LTO/ABI,
full-application recovery UI and final image acceptance remain pending. A separate
unexpected device restart during the earlier UI test is still under investigation;
these filesystem results neither explain nor resolve it. PGO is not retrained.
