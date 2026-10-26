# a6400 NAND Dump — Copy-Paste Commands

Each partition is: dd to /log, exit shell, pull to PC, re-enter shell, delete staging file.
Run each line one at a time. Wait for each to finish before the next.

---

## BATCH 1 — Small critical partitions

You should be in the BusyBox shell already (`shell` from PMCA prompt).

### nflasha2 — /setting (20 MB) — has Backup.bin

```
dd if=/dev/nflasha2 of=/log/dump.raw bs=4096
```

Press Ctrl+C to exit BusyBox, then at the PMCA `>` prompt:

```
pull /log/dump.raw nflasha2_setting.raw
```

```
shell
```

Now back in BusyBox:

```
rm /log/dump.raw
```

### nflasha7 — rootfs (8 MB)

```
dd if=/dev/nflasha7 of=/log/dump.raw bs=4096
```

Ctrl+C, then at PMCA `>`:

```
pull /log/dump.raw nflasha7_rootfs.raw
```

```
shell
```

```
rm /log/dump.raw
```

### nflasha6 — Jiritsu (16 MB)

```
dd if=/dev/nflasha6 of=/log/dump.raw bs=4096
```

Ctrl+C, then at PMCA `>`:

```
pull /log/dump.raw nflasha6_jiritsu.raw
```

```
shell
```

```
rm /log/dump.raw
```

### nflasha10 — /tmp + /etc (12 MB)

```
dd if=/dev/nflasha10 of=/log/dump.raw bs=4096
```

Ctrl+C, then at PMCA `>`:

```
pull /log/dump.raw nflasha10_tmp_etc.raw
```

```
shell
```

```
rm /log/dump.raw
```

### nflasha1 — Updater (8 MB)

```
dd if=/dev/nflasha1 of=/log/dump.raw bs=4096
```

Ctrl+C, then at PMCA `>`:

```
pull /log/dump.raw nflasha1_updater.raw
```

```
shell
```

```
rm /log/dump.raw
```

### nflasha4 — MPR Backup (12 MB)

```
dd if=/dev/nflasha4 of=/log/dump.raw bs=4096
```

Ctrl+C, then at PMCA `>`:

```
pull /log/dump.raw nflasha4_mpr_backup.raw
```

```
shell
```

```
rm /log/dump.raw
```

### nflasha13 — UpdaterWork (4 MB)

```
dd if=/dev/nflasha13 of=/log/dump.raw bs=4096
```

Ctrl+C, then at PMCA `>`:

```
pull /log/dump.raw nflasha13_updaterwork.raw
```

```
shell
```

```
rm /log/dump.raw
```

### nflasha23 — PMBP (2 MB)

```
dd if=/dev/nflasha23 of=/log/dump.raw bs=4096
```

Ctrl+C, then at PMCA `>`:

```
pull /log/dump.raw nflasha23_pmbp.raw
```

```
shell
```

```
rm /log/dump.raw
```

---

## BATCH 2 — Medium partitions

### nflasha3 — /system (48 MB)

```
dd if=/dev/nflasha3 of=/log/dump.raw bs=4096
```

Ctrl+C, then at PMCA `>`:

```
pull /log/dump.raw nflasha3_system.raw
```

```
shell
```

```
rm /log/dump.raw
```

### nflasha5 — Warm Boot Image (60 MB)

```
dd if=/dev/nflasha5 of=/log/dump.raw bs=4096
```

Ctrl+C, then at PMCA `>`:

```
pull /log/dump.raw nflasha5_wbi.raw
```

```
shell
```

```
rm /log/dump.raw
```

### nflasha12 — /cert (40 MB)

```
dd if=/dev/nflasha12 of=/log/dump.raw bs=4096
```

Ctrl+C, then at PMCA `>`:

```
pull /log/dump.raw nflasha12_cert.raw
```

```
shell
```

```
rm /log/dump.raw
```

### nflasha18 — /lens (20 MB)

```
dd if=/dev/nflasha18 of=/log/dump.raw bs=4096
```

Ctrl+C, then at PMCA `>`:

```
pull /log/dump.raw nflasha18_lens.raw
```

```
shell
```

```
rm /log/dump.raw
```

### nflasha17 — Android data (100 MB)

```
dd if=/dev/nflasha17 of=/log/dump.raw bs=4096
```

Ctrl+C, then at PMCA `>`:

```
pull /log/dump.raw nflasha17_android_data.raw
```

```
shell
```

```
rm /log/dump.raw
```

### nflasha16 — Android system (140 MB)

```
dd if=/dev/nflasha16 of=/log/dump.raw bs=4096
```

Ctrl+C, then at PMCA `>`:

```
pull /log/dump.raw nflasha16_android_system.raw
```

```
shell
```

```
rm /log/dump.raw
```

---

## BATCH 3 — Big partitions

### nflasha15 — /usr (300 MB)

```
dd if=/dev/nflasha15 of=/log/dump.raw bs=4096
```

Ctrl+C, then at PMCA `>`:

```
pull /log/dump.raw nflasha15_usr.raw
```

```
shell
```

```
rm /log/dump.raw
```

### nflasha11 — /log (500 MB) — dump LAST since we're staging TO /log

For this one we need to stage to /system (48 MB free) in two passes, 
or just skip the raw dump since /log is mostly logs you can pull as files.

If you want to try it (will write 500 MB to /system which is only 48 MB — WON'T FIT):

**Option A — Skip raw dump, pull log files individually instead:**

```
ls -laR /log/
```

Copy-paste that listing and we'll pull the interesting files.

**Option B — Pull Backup.bin as a regular file too:**

```
cp /setting/Backup.bin /log/Backup_file.bin
```

Ctrl+C, then at PMCA `>`:

```
pull /log/Backup_file.bin Backup.bin
```

```
shell
```

```
rm /log/Backup_file.bin
```

---

## BONUS — Grab /proc info and directory listings

```
cat /proc/cpuinfo > /log/proc_cpuinfo.txt
cat /proc/partitions > /log/proc_partitions.txt
cat /proc/mounts > /log/proc_mounts.txt
cat /proc/meminfo > /log/proc_meminfo.txt
cat /proc/cmdline > /log/proc_cmdline.txt
ls -laR / > /log/ls_root.txt
ls -laR /usr > /log/ls_usr.txt
ls -laR /setting > /log/ls_setting.txt
ls -laR /system > /log/ls_system.txt
ls -laR /tmp > /log/ls_tmp.txt
ls -laR /etc > /log/ls_etc.txt
ls -laR /log > /log/ls_log.txt
ls -laR /cert > /log/ls_cert.txt
ls -laR /lens > /log/ls_lens.txt
```

Ctrl+C, then at PMCA `>` pull them all:

```
pull /log/proc_cpuinfo.txt proc_cpuinfo.txt
pull /log/proc_partitions.txt proc_partitions.txt
pull /log/proc_mounts.txt proc_mounts.txt
pull /log/proc_meminfo.txt proc_meminfo.txt
pull /log/proc_cmdline.txt proc_cmdline.txt
pull /log/ls_root.txt ls_root.txt
pull /log/ls_usr.txt ls_usr.txt
pull /log/ls_setting.txt ls_setting.txt
pull /log/ls_system.txt ls_system.txt
pull /log/ls_tmp.txt ls_tmp.txt
pull /log/ls_etc.txt ls_etc.txt
pull /log/ls_log.txt ls_log.txt
pull /log/ls_cert.txt ls_cert.txt
pull /log/ls_lens.txt ls_lens.txt
```

Then `shell` and clean up:

```
rm /log/proc_cpuinfo.txt /log/proc_partitions.txt /log/proc_mounts.txt /log/proc_meminfo.txt /log/proc_cmdline.txt /log/ls_root.txt /log/ls_usr.txt /log/ls_setting.txt /log/ls_system.txt /log/ls_tmp.txt /log/ls_etc.txt /log/ls_log.txt /log/ls_cert.txt /log/ls_lens.txt
```

---

## When done

Exit BusyBox: Ctrl+C
Exit PMCA: `exit`

All files land in F:\a6400project\ (wherever you ran pmca-console from).
