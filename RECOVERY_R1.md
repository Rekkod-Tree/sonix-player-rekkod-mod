# Safe SD updates on HiBy R1

## Confirmed cause

The captured stock recovery rootfs contains `recoveryd` with `mount -rw /dev/%s /tmp/mnt/%s`. Its success and failure exit paths call `sync()` and the reboot syscall without an ordinary source-card unmount. This bypasses init's shutdown unmounts. The firmware ISO also keeps a loop-backed reference to the source file. This is a concrete mechanism for leaving FAT marked unclean, not proof that it caused every historical damaged cluster.

The normal Sonix `.upt` contains the normal kernel and rootfs. It does **not** update the independent recovery rootfs2, so changing only Sonix shutdown cannot close both stages.

## Changes

Sonix now requires a checked `syncfs`, closure of its root descriptor and successful ordinary unmount before arming recovery. Failure restores its own card claim and local services without rebooting. The boot selector is read back; failed recovery selection must be followed by a verified Normal restoration. An uncertain selector cancels the automatic reboot and disables idle auto-off in memory.

R1 updates also require the reviewed read-only recovery SHA-256. Unknown, unreadable and old recovery images stop before playback or card-service shutdown. Other models retain their existing update path and are not claimed verified by this R1 work.

The one-time recovery candidate changes one decompressed byte: `mount -rw` becomes `mount -r `, preserving command length. A same-size LZO replacement leaves every other file, filesystem record and partition byte outside the reviewed compressed extent unchanged. It addresses recoveryd's SD source mount; USB-storage mdev and arbitrary external mount-service requests are outside this acceptance.

## Reviewed repair utility

`sonix-player/tools/r1_recovery_patch.c` is built separately and is neither installed nor automatically invoked by the player. It shares the immutable sizes and hashes in `src/system/device/recovery_r1.h`.

- `--check`: read-only model, selector, NAND geometry, all 192 bad blocks, whole-partition and target-block hash checks.
- `--apply reviewed-block.bin`: requires separate human authorization for the inactive recovery repair. It validates the complete 128 KiB replacement, retains an exclusive device lock through readback, rechecks the writable descriptor and normal boot selector, erases exactly block 34 and writes exactly 64 pages. It then closes and reopens the raw character device to verify the complete new image.
- A wrong model/image/map, changed device node, busy lock or malformed replacement refuses writes. Partial writes and failed readback stop without retry, rollback, selector changes or reboot.

Example build from `sonix-player/`, using the configured MIPS toolchain:

```sh
mipsel-rockbox-linux-gnu-gcc -std=gnu11 -O2 -static -D_FILE_OFFSET_BITS=64 -I. \
  tools/r1_recovery_patch.c src/system/core/sha256.c -o r1_recovery_patch
```

Before an authorized apply, retain and rehash the original full rootfs2/kernel2 dumps and old eraseblock, establish normal boot and a single active MTD writer, and keep power stable. No card format or change to the normal OS/kernel is part of this repair. A failure after erase can leave recovery incomplete: keep the normal OS running, preserve diagnostics and review restoration from the verified original backup before entering recovery or retrying. The utility does not provide automatic rollback.

## Verification and remaining acceptance

Native fixtures cover update preparation/rollback, selector failures, the R1 image guard and 24 repair scenarios using real offline image/block hashes; all privileged operations are intercepted. MIPS compilation and ABI checks pass. Full extraction of both recovery images compared 1693 file paths, 134 directories and 482 symlinks; metadata and all content except the one byte match.

On the physical R1, the exact stock BusyBox loop command successfully read the real `.upt` from an isolated read-only card bind; `LO_FLAGS_READ_ONLY`, OTA metadata and cleanup were checked. The original card mount remained read/write. The compiled repair helper subsequently passed its actual `--check`: the raw original image, normal selector, exact geometry and all 192 good blocks matched. No erase/write or recovery boot was performed. This is not a complete update-cycle test.

The already-set FAT flag is not silently cleared. [Linux 4.4 deliberately preserves a volume that was dirty when mounted](https://github.com/torvalds/linux/blob/v4.4/fs/fat/inode.c#L533); a fresh offline filesystem check is needed to establish a clean baseline. After applying the repair, validate an actual update and subsequent mounts against that baseline. Preserve the earlier raw backup. Battery/long-playback acceptance remains separate.

Private artifacts and logs are under workspace `backups/r1-recovery-20261016/` and `output/recovery-ro-patch/`; proprietary recovery images and built helpers are not committed.
