# a6400 Full Storage Dump Guide

**Goal:** Copy everything off the camera's internal NAND to `F:\a6400project\camera_dump\` so all research happens on the PC copy, never on the live camera.

**Safety:** Every command below is **read-only**. We use `cat` to read block devices and `pull` to transfer files. Nothing writes to the camera.

---

## Prerequisites

1. Camera powered on, USB mode set to **Mass Storage**
2. USB cable connected to PC
3. Open a terminal in `F:\a6400project`
4. Start the service shell:
   ```
   pmca-console-v0.18-win.exe serviceshell
   ```
5. You should see the BusyBox root prompt (`/ #`)

---

## Partition Map Reference

From `mount.conf` and `partinf.conf`:

| Device | Mount | Type | Size | Description |
|--------|-------|------|------|-------------|
| nflasha1 | (none) | vfat | 8 MB | Updater |
| nflasha2 | /setting | vfat | 20 MB | Settings (has Backup.bin) |
| nflasha3 | /system | vfat | 48 MB | System main |
| nflasha4 | (none) | raw | 12 MB | MPR Flash Backup |
| nflasha5 | /wbi | wbi | 60 MB | Warm Boot Image |
| nflasha6 | (none) | vfat | 16 MB | Jiritsu |
| nflasha7 | / | ext2 | 8 MB | Rootfs |
| nflasha10 | /tmp | vfat | 12 MB | Work (persistent) |
| nflasha11 | /log | vfat | 500 MB | Log |
| nflasha12 | /cert | vfat | 40 MB | Network/certs |
| nflasha13 | (none) | vfat | 4 MB | UpdaterWork |
| nflasha15 | /usr | ext2 | 300 MB | User binaries |
| nflasha16 | /android/system | ext2 | 140 MB | Android (system) |
| nflasha17 | /android/data | vfat | 100 MB | Android (data) |
| nflasha18 | /lens | vfat | 20 MB | Lens data |
| nflasha23 | /PMBP | prfile | 2 MB | PMBP info/licenses |

Partitions 8, 9, 14, 19-22, 24-30 are zero-size (reserved/unused).

---

## Strategy: Two Approaches

### Approach A: Raw block-device dumps (recommended first)

This captures every byte of each partition, including deleted files, filesystem metadata, and anything not visible via `ls`. Essential for forensic research.

**How it works:** BusyBox `cat` can read `/dev/nflasha*` and we pipe the output via `pull` to the PC. The PMCA shell's `pull` command copies a file from the camera to the PC.

### Approach B: Filesystem-level file pulls

This copies the visible file/directory tree from each mounted partition. Easier to browse, but misses deleted files and raw structures.

**We do both** — raw dumps for completeness, file pulls for convenience.

---

## Part 1: Raw Block Device Dumps

For each non-zero partition, we dump the raw block device to `/tmp/` (the writable staging area), then `pull` it to the PC. Because `/tmp` is only 12 MB, we must dump one partition at a time and delete the staging file between dumps.

**Important:** For partitions larger than ~10 MB, we need to dump directly via `cat` and `pull` without staging to `/tmp`. The PMCA `pull` command can read directly from `/dev/nflasha*`.

### Step-by-step for each partition

Try `pull` directly from the device node first. In the service shell:

```sh
# Test if pull works directly on device nodes:
pull /dev/nflasha1 nflasha1_updater.raw
```

If direct `pull` from `/dev/` works, repeat for every non-zero partition:

```sh
pull /dev/nflasha1 nflasha1_updater.raw
pull /dev/nflasha2 nflasha2_setting.raw
pull /dev/nflasha3 nflasha3_system.raw
pull /dev/nflasha4 nflasha4_mpr_backup.raw
pull /dev/nflasha5 nflasha5_wbi.raw
pull /dev/nflasha6 nflasha6_jiritsu.raw
pull /dev/nflasha7 nflasha7_rootfs.raw
pull /dev/nflasha10 nflasha10_tmp.raw
pull /dev/nflasha11 nflasha11_log.raw
pull /dev/nflasha12 nflasha12_cert.raw
pull /dev/nflasha13 nflasha13_updaterwork.raw
pull /dev/nflasha15 nflasha15_usr.raw
pull /dev/nflasha16 nflasha16_android_system.raw
pull /dev/nflasha17 nflasha17_android_data.raw
pull /dev/nflasha18 nflasha18_lens.raw
pull /dev/nflasha23 nflasha23_pmbp.raw
```

**If direct `pull` from /dev/ does NOT work**, use the staging method for small partitions (under 10 MB) and `dd` piped through the shell for larger ones:

```sh
# Small partitions — stage to /tmp, pull, delete:
cat /dev/nflasha1 > /tmp/dump.raw
pull /tmp/dump.raw nflasha1_updater.raw
rm /tmp/dump.raw

cat /dev/nflasha7 > /tmp/dump.raw
pull /tmp/dump.raw nflasha7_rootfs.raw
rm /tmp/dump.raw

cat /dev/nflasha13 > /tmp/dump.raw
pull /tmp/dump.raw nflasha13_updaterwork.raw
rm /tmp/dump.raw

cat /dev/nflasha23 > /tmp/dump.raw
pull /tmp/dump.raw nflasha23_pmbp.raw
rm /tmp/dump.raw
```

For larger partitions, see if `dd` with a count works to chunk them, or if the shell supports piping to `pull`. If neither works, skip raw dumps for the biggest partitions and rely on the filesystem pulls for those.

### Total raw dump sizes

All non-zero partitions total about **1.2 GB** — easily fits on the PC.

---

## Part 2: Filesystem-Level File Pulls

For each mounted filesystem, we pull the entire directory tree. This gives you browsable files.

### High-priority targets

```sh
# /setting — contains Backup.bin and camera settings
pull /setting/Backup.bin Backup.bin

# Pull entire /setting directory
# (pull may not support directories — test with a single file first)
pull /setting/ setting_dump/
```

If `pull` doesn't support directories, list and pull files individually:

```sh
# List what's there
ls -la /setting/
ls -laR /setting/

# Pull each file by name, e.g.:
pull /setting/Backup.bin setting/Backup.bin
pull /setting/SysBackup.bin setting/SysBackup.bin
# ... (repeat for each file listed)
```

### System inventory commands

Run these first to capture the full picture. Save the output by copy-pasting from the terminal:

```sh
# Full mount table
mount

# All block devices
ls -la /dev/nflasha*

# Disk usage
df

# Running processes
ps

# Full filesystem listing for each mount
ls -laR / > /tmp/ls_root.txt
pull /tmp/ls_root.txt ls_root.txt
rm /tmp/ls_root.txt

ls -laR /usr > /tmp/ls_usr.txt
pull /tmp/ls_usr.txt ls_usr.txt
rm /tmp/ls_usr.txt

ls -laR /setting > /tmp/ls_setting.txt
pull /tmp/ls_setting.txt ls_setting.txt
rm /tmp/ls_setting.txt

ls -laR /system > /tmp/ls_system.txt
pull /tmp/ls_system.txt ls_system.txt
rm /tmp/ls_system.txt

ls -laR /tmp > /tmp/ls_tmp.txt
pull /tmp/ls_tmp.txt ls_tmp.txt
rm /tmp/ls_tmp.txt

ls -laR /log > /tmp/ls_log.txt
pull /tmp/ls_log.txt ls_log.txt
rm /tmp/ls_log.txt

ls -laR /cert > /tmp/ls_cert.txt
pull /tmp/ls_cert.txt ls_cert.txt
rm /tmp/ls_cert.txt

ls -laR /lens > /tmp/ls_lens.txt
pull /tmp/ls_lens.txt ls_lens.txt
rm /tmp/ls_lens.txt
```

### /etc (lives on nflasha10 alongside /tmp)

```sh
ls -laR /etc > /tmp/ls_etc.txt
pull /tmp/ls_etc.txt ls_etc.txt
rm /tmp/ls_etc.txt

# Key files to pull individually:
pull /etc/fstab etc_fstab.txt
pull /etc/passwd etc_passwd.txt
pull /etc/group etc_group.txt
pull /etc/init.d etc_initd/
# If pull doesn't do dirs:
ls /etc/init.d/
# then pull each script
```

### /proc virtual filesystem (runtime info, no raw dump needed)

```sh
cat /proc/cpuinfo > /tmp/cpuinfo.txt
pull /tmp/cpuinfo.txt proc_cpuinfo.txt
rm /tmp/cpuinfo.txt

cat /proc/meminfo > /tmp/meminfo.txt
pull /tmp/meminfo.txt proc_meminfo.txt
rm /tmp/meminfo.txt

cat /proc/cmdline > /tmp/cmdline.txt
pull /tmp/cmdline.txt proc_cmdline.txt
rm /tmp/cmdline.txt

cat /proc/mtd > /tmp/mtd.txt
pull /tmp/mtd.txt proc_mtd.txt
rm /tmp/mtd.txt

cat /proc/mounts > /tmp/mounts.txt
pull /tmp/mounts.txt proc_mounts.txt
rm /tmp/mounts.txt

cat /proc/partitions > /tmp/partitions.txt
pull /tmp/partitions.txt proc_partitions.txt
rm /tmp/partitions.txt
```

---

## Part 3: Priority Order

If you want to do this in stages, here is the priority:

### Stage 1 — Critical (do first, ~5 minutes)
1. `/setting/Backup.bin` — the settings database, likely contains hidden flags
2. `/proc/` info files (cpuinfo, meminfo, cmdline, mtd, mounts, partitions)
3. `ls -laR` listings for every mount point
4. `mount` output

### Stage 2 — High value (~20 minutes)
5. Raw dump of nflasha2 (/setting, 20 MB)
6. Raw dump of nflasha6 (Jiritsu, 16 MB)
7. Raw dump of nflasha7 (rootfs, 8 MB)
8. Raw dump of nflasha10 (/tmp + /etc, 12 MB)
9. All files from /etc/ directory
10. All files from /setting/ directory

### Stage 3 — Full dump (~1-2 hours depending on USB speed)
11. Raw dump of nflasha3 (system, 48 MB)
12. Raw dump of nflasha15 (usr, 300 MB)
13. Raw dump of nflasha11 (log, 500 MB)
14. Everything else

---

## Part 4: Verification

After each pull, verify the file arrived and has a reasonable size:

On Windows (PowerShell), in `F:\a6400project`:
```powershell
# Check sizes of all dumped files
Get-ChildItem *.raw, *.txt, *.bin | Format-Table Name, Length
```

For raw dumps, compare the file size to the expected partition size from the table above. They should match exactly for raw type partitions, or be close for vfat partitions.

For critical files, compute checksums:
```powershell
Get-FileHash Backup.bin -Algorithm SHA256
Get-FileHash nflasha2_setting.raw -Algorithm SHA256
```

You can also verify on-camera before pulling (if `md5sum` or `sha256sum` is available in BusyBox):
```sh
md5sum /setting/Backup.bin
```

Then compare with the PC-side checksum after transfer.

---

## Safety Checklist

Before each command, verify:
- [ ] You are only using `cat`, `ls`, `pull`, `dd if=` (read), `mount` (check) — never `write`, `dd of=`, `mkfs`, `mount -o remount,rw`
- [ ] Any file you write to `/tmp/` is a temporary staging copy, deleted after pull
- [ ] You never redirect output to any path other than `/tmp/`
- [ ] You never run any firmware update or flash commands

**If anything looks wrong, just close the terminal.** The service shell exits cleanly and the camera continues normally.

---

## Notes

- USB transfer speed via PMCA service shell is relatively slow (often under 1 MB/s). The full 1.2 GB dump may take 30+ minutes.
- The camera should stay powered on throughout. Use AC adapter if available to avoid battery drain.
- `/tmp` (nflasha10) is our only writable staging area and it's only 12 MB, so always clean up staging files.
- The firmware is already extracted at `F:\a6400project\fw_v200_extracted\` — compare raw dumps against those to find differences between the firmware image and what's actually on the camera.
