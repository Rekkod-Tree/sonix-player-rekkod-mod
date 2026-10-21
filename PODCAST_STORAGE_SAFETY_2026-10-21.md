# Podcast download storage isolation

Issue #29 fixes a file-lifetime defect: an old download retained absolute paths
and could create audio, tags and artwork on a replacement card at the same mount
point. The existing podcast-state epoch protected database callbacks only.

## Contract

- A job captures the admitted card epoch and root device/inode. Each local phase
  reacquires `storage_pin_card`, validates that identity, and uses directory-relative
  operations. No mutation resolves an old absolute destination again.
- Network connect, body reads and artwork fetch hold no card descriptors. Local
  writes use bounded 32 KiB chunks; USB does not wait for a whole network transfer.
- Unique exclusive staging files and inode checks prevent append, publication or
  cleanup of somebody else's replacement file. Card detach requests nonblocking
  cancellation before closing podcast state, without joining under the card lock.
- Publication checks file flush/sync errors. Unsupported directory sync is
  tolerated; genuine errors fail completion accounting. Artwork remains optional.
- A detached old card can retain its hidden partial file. Cleanup deliberately
  refuses to follow its former pathname onto a different card.

The storage manager must invalidate the epoch before a runtime remount, including
recovery after a missed removal event. Root device/inode is defense in depth, not
a FAT volume identifier: different cards can reuse both values. Startup mounting
and duplicate insertion events must not accidentally stop valid playback.
Runtime `mount_sd` now detaches at its actual dead/absent-volume replacement
branches under the existing card lock; startup explicitly skips service teardown.
The hotplug receive buffer also reserves its terminator byte (issue #31).

No new worker, polling timer, library or persistent format is introduced. Caller-owned
path snapshots and podcast-state ordering from previous fixes remain intact.

## Verification

The independent baseline test reproduced audio, tags and cover writes to a synthetic
replacement card. Frozen v2 passed 21 independent barrier/fault scenarios and ten
author scenarios with ASAN/UBSAN/LSAN. These include root replacement, admitted
descriptor operations, replaced staging inode/symlink, cancellation and I/O faults.
Network fixtures assert no retained filesystem descriptors; all exits balance them.

On R1, the same static MIPS harnesses passed 31 isolated scenarios in temporary
directories. Four additional scenarios exercised actual FAT operations in an owned
hidden test directory: success, injected ENOSPC, cancellation and a 2 MiB transfer.
HTTP and storage-manager admission were mocked; the test did not remove the user's
card or force an unmount. Test files were archived, verified and removed.

Full application, target/ABI and package verification are separate delivery gates;
the host integration on both panel sizes, LTO target/ABI and exact-binary R1 smoke
passed. Home, services, local podcasts/settings, mini-player and playback were
checked. The installed alpha21, all settings and three databases were restored;
owned temporary files were archived, verified and removed. No reboot was issued.
The release manifest records final package verification. PGO is not retrained.

Eight additional tests compile the actual mount branch with explicit system
fixtures, checking missed-remove recovery, startup, duplicate events and failures.
The actual receive statement is checked at its maximum size. Sanitizers pass;
old-code controls fail the epoch-order assertion and buffer-bound check. These
prove application branch ordering, not physical hotplug delivery.

## Remaining limits

Issue #30 separately tracks two-file publication: a sidecar rename error after
audio rename can leave new audio with an old sidecar. The job reports failure and
does not notify the database; audio and tags are not an atomic transaction.

Initial connect and optional cover cancellation can wait for the existing bounded
HTTP timeout, but retain no card descriptors. Full-image boot, physical hot removal,
power-loss durability, Bluetooth stability and battery runtime are not established
by these harnesses.
