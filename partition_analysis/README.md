# a6400 Full Partition Analysis — Homebrew Entry Points

**Date:** 2026-10-26
**Camera:** Sony ILCE-6400 (ILC181h), Firmware v2.00
**SoC:** CXD90045-FPSU (ARMv7, 4-core), Linux 3.0.27

---

## Executive Summary

Analysis of all 16 NAND partitions from a Sony a6400 reveals **one confirmed code injection path** and several supporting findings:

1. **PRIMARY: `/setting/env.txt` → LD_PRELOAD injection** (confirmed in init binary)
2. `/etc/ld.so.preload` referenced by ld-linux.so but /etc is read-only (blocked without further exploit)
3. Jiritsu partition contains factory scripting engine with file write capabilities
4. nflasha10 (/tmp) is writable but has no useful injection hooks

---

## The env.txt Injection Path (PRIMARY FINDING)

### Discovery

The `/sbin/init` binary (26,101 bytes, ARM ELF) on the rootfs (nflasha7) contains a function called `set_oreore_env` that:

1. Opens `/setting/env.txt`
2. Calls `parse_and_set_env` to parse each line
3. Calls `putenv()` to set each variable as an environment variable
4. All subsequent processes launched by init inherit these variables

### Why This Works

- `/setting` is mounted from **nflasha2** (20 MB FAT12/vfat) — **writable**
- `/setting/env.txt` does **NOT exist** on the current partition (confirmed by raw binary search of nflasha2.img)
- Init already uses LD_PRELOAD for Sony debug libraries, proving the mechanism works:
  - `LD_PRELOAD=/usr/tool/libkmcsup.so.2.0.0`
  - `LD_PRELOAD=/usr/tool/libsonyefence.so`
  - `LD_PRELOAD=/usr/tool/libduma.so`

### Exploitation

**Step 1:** Create a custom ARM shared library (.so) that runs your code in its constructor:

```c
// payload.c — cross-compile for ARMv7
#include <stdio.h>
#include <stdlib.h>
__attribute__((constructor))
void payload_init(void) {
    // Your code here — runs in every process that loads this .so
    // Example: write a flag file to confirm execution
    FILE *f = fopen("/tmp/homebrew_active", "w");
    if (f) { fprintf(f, "loaded\n"); fclose(f); }
}
```

**Step 2:** Cross-compile:
```sh
arm-linux-gnueabi-gcc -shared -fPIC -o payload.so payload.c -nostdlib -lgcc
```

**Step 3:** Push the .so to a writable path on the camera:
```sh
# Via PMCA service shell:
push payload.so /tmp/payload.so
```

**Step 4:** Create env.txt on /setting:
```sh
# In service shell:
shell
echo 'LD_PRELOAD=/tmp/payload.so' > /setting/env.txt
sync
```

**Step 5:** Reboot the camera. On next boot, init reads env.txt, sets LD_PRELOAD, and every process launched by init will load payload.so.

### Risks & Caveats

- **Recovery:** If payload.so crashes init, the camera may not boot. Keep a clean firmware update file ready.
- **Persistence:** `/tmp` (nflasha10) is writable and persistent across boots, but files placed via `push` live in the vfat filesystem. The .so MUST be at the path specified in env.txt when init runs.
- **Better persistence:** Place the .so on `/setting` itself (also writable, FAT12):
  ```
  LD_PRELOAD=/setting/payload.so
  ```
- **Process filtering:** The .so loads into EVERY process. Use `getpid()` == 1 or check `/proc/self/comm` to limit actions to specific processes.

---

## Boot Sequence (from init binary analysis)

```
1. insmod /kmod/devno.ko
2. insmod /kmod/dmac.ko
3. insmod /kmod/ldec.ko
4. insmod /kmod/emmc.ko
5. Mount partitions:
   nflasha2  → /setting     (vfat, writable)
   nflasha3  → /system      (vfat)
   nflasha10 → /tmp         (vfat, writable)
   nflasha15 → /usr         (ext2/cramfs)
   nflasha12 → /cert        (vfat)
   nflasha18 → /lens        (vfat)
   nflasha11 → /log         (vfat, writable)
   nflasha7  → /            (ext2, read-only)
6. Check /setting/mode/dmode and /setting/sen/smode (boot mode flags)
7. Read /setting/env.txt → set_oreore_env() → putenv() ← INJECTION POINT
8. Set default env: HOME=/, PATH=/sbin:/bin:/usr/sbin:/usr/bin:/devel/usr/bin
9. Set DmmConfig=/setting/DmmConfig.bin, ulogio_initfile=/setting/ulogio.bin
10. Launch /usr/bin/bootin.elf (main camera application)
    Also: /usr/bin/sen.elf, /usr/bin/net_cmdd.elf, /usr/bin/launch_shell.elf
11. /usr/bin/swap_prepare.sh
```

### Boot Mode Flags

Init checks two files for special boot modes:
- `/setting/sen/smode` — "sen mode" (service/engineering mode?)
- `/setting/mode/dmode` — "debug mode"?

These may enable additional functionality. Both paths are on the writable /setting partition.

---

## Partition Analysis Details

### nflasha7 — Rootfs (8 MB, ext2, read-only)

Root directory contents:
```
/bin/        /cert/       /dev/        /etc/  (empty dir)
/initrd/     /kmod/       /lens/       /lib/
/log/        /mnt/        /network@    /proc/
/root/       /sbin/       /setting/    /sys/
/system/     /tmp/        /tmp_bt/     /tmp_cert/
/tmp_network/ /tmp_updater/ /usr/      /var/
version.txt
```

Key binaries in `/sbin/`:
- **init** (26,101 bytes) — the main init process, analyzed above
- Kernel modules in `/kmod/`: devno.ko, dmac.ko, ldec.ko, emmc.ko

Key finding: `/etc/` is an empty directory on the read-only rootfs. The dynamic linker (ld-linux.so) references `/etc/ld.so.preload` but since /etc is not writable, this path is blocked unless a writable filesystem is mounted over it.

### nflasha10 — /tmp (12 MB, FAT12, writable)

Contents:
```
dhcpd.conf        — ISC DHCP sample config (template, unused)
mtab               — symlink content: "../proc/mounts"
toprc              — top(1) config (init sets TOPRC=/etc/toprc)
resolv.conf        — "search home\nnameserver 192.168.0.1"
hello_armv7.elf    — our test binary from earlier
kikilog.dat        — 1.9 MB binary log (kiki logging subsystem)
IMAGES/            — font/image files (MyIngH*.ltt, Sony_D*.ttf)
SDCARD/            — empty directory
```

**No ld.so.preload or injection hooks found.** This partition is writable and persistent, making it a good location to store payload files.

### nflasha6 — Jiritsu (16 MB, FAT12)

The Jiritsu partition contains Sony's factory testing/calibration system:

**Files (from manifest):**
```
jiritsu_20200210_01_DX2_COMMON.tar.dat  — test scripts archive
tmmc.jit                                 — compiled test scripts
libJit.so (311 KB)                       — ARM shared object, scripting engine
DIAC.csv                                 — calibration data
TOOLBAR_VERSION = 18.02.06
+ Factory adjustment spreadsheets for ILC181h (a6400)
```

**libJit.so Scripting Commands:**
The Jiritsu scripting engine (CDiScript/CScriptInt classes) exposes ~100 commands including:

| Command | Purpose |
|---------|---------|
| `@JIRITSU_SET_FILE` | Write files to flash |
| `@JIRITSU_GET_FILE` | Read files from flash |
| `@JIRITSU_SET_FILE_PATH` | Set target file path |
| `@JIRITSU_FILE_DELETE` | Delete files |
| `@JIRITSU_MOUNT` | Mount partitions |
| `@JIRITSU_FORMAT` | Format partitions |
| `@JIRITSU_FIRMUP` | Firmware update |
| `@UD_REBOOT` | Reboot camera |
| `@UD_RUN_PRG` | **Run a program** |
| `Backup_read` / `Backup_write` | Read/write settings |
| `@RAM_READ` / `@RAM_WRITE` | Direct memory access |
| `@REG_READ` / `@REG_WRITE` | Register access |
| `@EXEC_SYS_I2C_READ/WRITE` | I2C bus access |

**Notable:** `@UD_RUN_PRG` can execute programs, and `@JIRITSU_SET_FILE` can write files — if the Jiritsu scripting engine can be activated, it provides another code execution path.

### nflasha2 — /setting (20 MB, FAT12, writable)

Contains Backup.bin (settings database, 1.1 MB), DmmConfig.bin, ulogio.bin, and various config directories. **env.txt does NOT exist** — creating it is the injection path.

### nflasha1 — Updater (8 MB, vfat)
Firmware update mechanism. Not analyzed in depth.

### nflasha3 — /system (48 MB, vfat)
Main system partition. Not yet analyzed.

### nflasha15 — /usr (300 MB, ext2)
**HIGH PRIORITY for future analysis.** Contains:
- `/usr/bin/bootin.elf` — main camera application
- `/usr/bin/sen.elf` — sensor/service mode binary
- `/usr/bin/net_cmdd.elf` — network command daemon
- `/usr/bin/launch_shell.elf` — shell launcher
- `/usr/tool/libkmcsup.so.2.0.0` — debug library (LD_PRELOAD target)
- `/usr/tool/libsonyefence.so` — Sony electric fence
- `/usr/tool/libduma.so` — malloc debugger
- `/usr/kmod/*.ko` — all kernel modules (30+)

Analyzing bootin.elf and the /usr/tool/ libraries would reveal what the debug LD_PRELOAD libraries do and how to hook into the camera's main process.

### Other Partitions
- **nflasha11** (/log, 500 MB) — log storage, writable
- **nflasha12** (/cert, 40 MB) — certificates/network config
- **nflasha13** (UpdaterWork, 4 MB) — firmware update workspace
- **nflasha16** (Android system, 140 MB) — vestigial Android
- **nflasha17** (Android data, 100 MB) — vestigial Android
- **nflasha18** (/lens, 20 MB) — lens calibration data
- **nflasha23** (PMBP, 2 MB) — license/info

---

## All Known Injection Paths (Ranked)

### 1. `/setting/env.txt` LD_PRELOAD — CONFIRMED, READY TO TEST
- **Difficulty:** Low
- **Persistence:** Survives reboot (env.txt and .so on writable partitions)
- **Risk:** Medium (bad .so can prevent boot; firmware reflash recovers)
- **Mechanism:** Init reads env.txt → putenv() → LD_PRELOAD loads .so into all processes

### 2. `/setting/sen/smode` and `/setting/mode/dmode` — NEEDS TESTING
- **Difficulty:** Low (just create files on /setting)
- **Persistence:** Survives reboot
- **Risk:** Unknown — may enable service/debug features or change boot flow
- **Mechanism:** Init checks these files for special boot modes

### 3. Jiritsu `@UD_RUN_PRG` / `@JIRITSU_SET_FILE` — NEEDS RESEARCH
- **Difficulty:** High (need to trigger Jiritsu scripting engine from service shell)
- **Persistence:** Could write to any partition
- **Risk:** Low if controlled
- **Mechanism:** Factory test system has file write and program execution commands

### 4. `/etc/ld.so.preload` — BLOCKED
- **Difficulty:** N/A — /etc is on read-only rootfs
- **Workaround possibility:** If a writable fs could be mounted over /etc, this would work. The dynamic linker (ld-linux.so) reads this file for every dynamically-linked program.

### 5. Backup.bin Setting Flags — EXPLORATORY
- **Difficulty:** Medium (need to map 2-byte on-disk IDs to 4-byte runtime IDs)
- **76 boolean OFF flags** that could potentially enable hidden features
- **6 boolean ON flags** currently active

---

## Recommended Next Steps

### Immediate (via existing service shell)

1. **Test env.txt injection:**
   - Cross-compile a minimal ARM .so with a constructor that writes a flag file
   - Push it to /setting/payload.so
   - Create /setting/env.txt with `LD_PRELOAD=/setting/payload.so`
   - Reboot and check /tmp/homebrew_active

2. **Test boot mode flags:**
   - `cat /setting/sen/smode` and `cat /setting/mode/dmode` (check if they exist)
   - Try creating them with various values (0, 1, "service", etc.)

3. **Check live /etc:**
   - Run `ls -la /etc/` in service shell to see if anything populates /etc at runtime
   - Run `cat /proc/mounts` to see all active mounts

### Short-term

4. **Analyze /usr partition (nflasha15, 300 MB):**
   - Extract and reverse-engineer bootin.elf (main camera app)
   - Examine /usr/tool/ debug libraries
   - Map kernel modules

5. **Cross-compile ARM toolchain:**
   - Set up arm-linux-gnueabi-gcc for the camera's toolchain (GCC 4.5.1, glibc 2.4)
   - Build a proper payload .so

### Medium-term

6. **Build a homebrew launcher:**
   - .so loaded via LD_PRELOAD that provides a menu or telnet daemon
   - Hook into bootin.elf's event loop for UI integration
   - Or run a telnet server on a network port

---

## Tools & Files

| File | Description |
|------|-------------|
| `analyze_backup_bin.py` | Backup.bin parser (search, CSV export, bool flags) |
| `backup_bin_settings.csv` | All 17,433 settings with values |
| `DUMP_COMMANDS.md` | Partition dump commands for service shell |
| `DUMP/` (on device) | All 16 partition images |

---

## References

- [Sony-PMCA-RE](https://github.com/ma1co/Sony-PMCA-RE) — PMCA reverse engineering
- [OpenMemories-Tweak](https://github.com/ma1co/OpenMemories-Tweak) — does NOT work on a6400
- [Language unlock gist](https://gist.github.com/gibilli111/eed521cfa7c0be8be8032225da890daa)
- Sony-PMCA-RE issues: #381 (no tweaks on a6400), #513 (service mode), #733 (backup crash)
