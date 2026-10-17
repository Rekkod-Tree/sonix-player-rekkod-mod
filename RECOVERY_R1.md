# Safe SD updates on HiBy R1

## Confirmed cause

The captured stock recovery rootfs contains `recoveryd` with `mount -rw /dev/%s /tmp/mnt/%s`. Its success and failure exit paths call `sync()` and the reboot syscall without an ordinary source-card unmount. This bypasses init's shutdown unmounts. The firmware ISO also keeps a loop-backed reference to the source file. This is a concrete mechanism for leaving FAT marked unclean, not proof that it caused every historical damaged cluster.

The normal Sonix `.upt` contains the normal kernel and rootfs. It does **not** update the independent recovery rootfs2, so changing only Sonix shutdown cannot close both stages.

## Changes

Sonix now requires a checked `syncfs`, closure of its root descriptor and successful ordinary unmount before arming recovery. Failure restores its own card claim and local services without rebooting. The boot selector is read back; failed recovery selection must be followed by a verified Normal restoration. An uncertain selector cancels the automatic reboot and disables idle auto-off in memory.

R1 updates also require the reviewed read-only recovery SHA-256. Unknown, unreadable and old recovery images stop before playback or card-service shutdown. Other models retain their existing update path and are not claimed verified by this R1 work.

The one-time recovery candidate changes one decompressed byte: `mount -rw` becomes `mount -r `, preserving command length. A same-size LZO replacement leaves every other file, filesystem record and partition byte outside the reviewed compressed extent unchanged. It addresses recoveryd's SD source mount; USB-storage mdev and arbitrary external mount-service requests are outside this acceptance.

## Reviewed repair utility

`sonix-player/tools/r1_recovery_patch.c` is built separately as `sonix_recovery_r1` and installed only in the R1 image. The on-device wizard invokes it after a verified backup and a separate repair confirmation; neither boot nor ordinary firmware installation automatically applies it. It shares the immutable sizes and hashes in `src/system/device/recovery_r1.h`.

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

### Public update preparation

Open **Settings > System > Safe R1 updates**. Identification is read-only. An
already prepared image needs no repair. For the single supported original image,
the wizard saves and reads back both recovery partitions in a new
`RecoveryBackup/r1-.../` directory on the card (at least 40 MB free), then offers
the separate repair confirmation. Applying requires external power and at least
50% battery. Unknown images stay blocked. Do not delete the verified backups.

After an uncertain write/readback, normal shutdown, standby and software reboot
remain blocked: keep the player powered and seek recovery assistance. There is
no automatic second erase or rollback. Hardware power loss cannot be prevented
by the UI. The tested R1 is already repaired; the wizard must not rewrite it.

The first installation from an older, unguarded firmware still uses that old
firmware's recovery. The new wizard protects subsequent updates and cannot
retroactively prevent an unclean flag from that first transition. It also does
not repair pre-existing FAT damage. An alpha15 installation that already blocks
its stock recovery needs a separate supported transition; renaming the image
does not bypass that guard.

For reproducible packaging, derive the private 128 KiB input from the exact
supported original backup using
`python3 sonix-player/tools/prepare_r1_recovery_block.py rootfs2.bin r1-recovery-ro.bin`.
The host-only generator checks the complete old image, expected bytes, new
block and complete candidate hashes, and refuses existing output files. It
never writes device nodes. Pass the resulting path as `R1_RECOVERY_BLOCK` to
the packer; raw firmware backups and the generated block remain outside Git.

The user subsequently installed the staged alpha13 image through the repaired recovery. On the next normal boot, installed version/build and executable SHA-256 matched the final alpha13 build; the card image also matched. The fresh kernel log had no unclean-volume warning, invalid cluster, I/O error or read-only remount, and FAT32 remained read/write. The recovery's complete raw hash and target block passed a separate `--check` again; bad-block/ECC counters stayed zero. The recovered full FLAC matched its original verified hash and the library opened with 695 indexed tracks. This accepts the tested SD-update cycle from alpha12 into alpha13. The new alpha13 pre-arm failure paths have native fixture coverage; repeated future cycles and unrelated USB update sources are not claimed here.

The user explicitly authorized the one-time repair after preparation. The utility applied eraseblock 34 on the R1, verified the complete raw partition, and a separate `--check` reported the repaired image already present. The full SHA-256 matched `259b7d112b0bb49277dbf2dc9008411ddd839563601d17169dd97a428b8e56a0`; bad-block and ECC-failure counters remained zero. The normal boot selector was unchanged and alpha12 kept running. No reboot was requested; the temporary helper and replacement block were removed. Evidence: workspace `output/recovery-ro-patch/helper-device-{preapply,apply,postapply}.log` and `application-result.json`.

Native fixtures cover update preparation/rollback, selector failures, the R1 image guard and 24 repair scenarios using real offline image/block hashes; all privileged operations are intercepted. MIPS compilation and ABI checks pass. Full extraction of both recovery images compared 1693 file paths, 134 directories and 482 symlinks; metadata and all content except the one byte match.

Before installation, the exact stock BusyBox loop command successfully read the real `.upt` from an isolated read-only card bind on R1; `LO_FLAGS_READ_ONLY`, OTA metadata and cleanup were checked. The original card mount remained read/write. The compiled repair helper also passed its actual pre-apply `--check`: the raw original image, normal selector, exact geometry and all 192 good blocks matched. These preparatory checks were subsequently followed by the accepted update cycle described above.

The already-set FAT flag is not silently cleared. [Linux 4.4 deliberately preserves a volume that was dirty when mounted](https://github.com/torvalds/linux/blob/v4.4/fs/fat/inode.c#L533). Following the repair, the user moved the card to a PC reader. A further 554 MB of new/changed/important files was preserved and verified, all four copied databases passed integrity checks, and read-only/repair/final CHKDSK runs returned 0 without structural errors. Preserved files remained unchanged. Alpha13 was hash-verified on the card; after flushing and clean dismount a raw boot-sector read confirmed the dirty flag false, then safe eject was confirmed. No flag was manually edited. This establishes the offline baseline for the subsequently accepted cycle. Evidence: workspace `backups/r1-card-post-recovery-20261016/RESULT.md`. Preserve the earlier raw backup. Battery/long-playback acceptance remains separate.

Private artifacts and logs are under workspace `backups/r1-recovery-20261016/` and `output/recovery-ro-patch/`; proprietary recovery images and built helpers are not committed.
