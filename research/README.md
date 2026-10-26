# a6400 Homebrew Research Handoff v3
**Date:** 2026-10-26
**Camera:** Sony ILCE-6400, Firmware v2.00
**Platform:** CXD90045 (Astra), 4x Cortex-A5, BIONZ X

## What's Confirmed Working

### Service Shell Access
- **Tool:** `pmca-console-v0.18-win.exe serviceshell` (from Sony-PMCA-RE)
- **USB mode:** Mass Storage (set in camera menu)
- **Result:** Root shell via BusyBox 1.13.4 (ash)
- **Kernel:** Linux 3.0.27_nl-rt106+ SMP PREEMPT RT, ARMv7 Cortex-A5
- **Working dir on entry:** `/usr/share/app`

### Native Code Execution
- A custom static ARMv7 ELF (`hello_armv7.elf`, 856 bytes, raw syscalls) was pushed to `/tmp/hello_armv7.elf`, chmod 700, and executed successfully. Output: "Hello, world!"

### Mount Layout (Live Camera)
| Partition | Mount | Type | RW? | Notes |
|-----------|-------|------|-----|-------|
| nflasha2 | /setting | vfat | rw | Camera settings |
| nflasha3 | /system | vfat | rw | System data |
| nflasha7 | / (rootfs) | ext2 | **ro** | Root filesystem |
| nflasha10 | /tmp | vfat | **rw** | "Work" partition, ~12MB, **persistent** (survives reboot) |
| nflasha11 | /log | vfat | rw | 500MB log partition |
| nflasha12 | /cert | vfat | rw | 40MB network certs |
| nflasha15 | /usr | ext2 | **ro** | 300MB, apps/libs/UI |
| nflasha16 | /android/system | ext2 | ro | 140MB, Android system (likely vestigial) |
| nflasha17 | /android/data | vfat | rw | 100MB, Android data |
| nflasha18 | /lens | vfat | rw | Lens calibration data |
| nflasha23 | /PMBP | prfile | - | 2MB, PlayMemories info |

---

## The Core Question: Launching Homebrew from the Camera GUI

### Why This Is Hard on the a6400

The a6400 does **not** support PlayMemories Camera Apps. Sony removed the PMCA app platform from all cameras released after roughly 2018. On older cameras (a6000, a5100, RX100 series, etc.), PMCA apps were Android APKs that ran in a Dalvik VM and appeared in the camera's "Application List" menu. The a6400 still has vestigial Android partitions (nflasha16/17) but the app framework is disabled.

This means:
- **No "Application List" menu item** exists on the a6400
- **No Android app install path** (the `pmca-console install` command won't work)
- **No OpenMemories-Tweak** (it's a PMCA app; can't be installed)
- The camera's menu system is compiled native code (C++), not a scripted UI

### The Camera's UI Architecture

From firmware analysis:
- **Menu views** are defined in `.uxc` files in `/usr/share/app/` (e.g., `viewGlobalMenu.uxc`, `viewSettingMenu.uxc`, `viewFnMenu.uxc`)
- These are binary resources, not human-editable text
- The menu logic is in **libObj.so** (the "Object" library), which contains the view/model MVC framework
- Input uses a "belt" model: directional navigation, not touch coordinates
- Each view has a 16-action dispatch table
- The entire UI partition (`/usr`, nflasha15) is **read-only ext2**

### Key Libraries on the a6400
- **libJiritsu.so** - Present in `/usr/lib/`. This is the Jiritsu subsystem (related to camera boot/initialization)
- **libObj.so** - The UI object framework library

---

## Realistic Paths to GUI-Launched Homebrew

### Path 1: Backup.bin Settings Manipulation (Most Promising Short-Term)
**Difficulty:** Medium | **Risk:** Low-Medium | **Reversibility:** Yes

The camera stores ~15,000 settings entries in `Backup.bin` (in `/setting/`). On PMCA cameras, OpenMemories-Tweak modifies this file to enable hidden features like telnet and ADB daemons.

**On the a6400:**
- The telnet/ADB daemon binaries may still exist in the firmware even though the app to enable them is gone
- If the settings entries for enabling telnet/ADB still exist in the Backup.bin schema, writing to them directly (via service shell) could start network daemons
- This wouldn't add a menu item, but would give persistent wireless shell access after boot
- **Next step:** Dump `/setting/Backup.bin` via service shell and analyze its structure. Look for telnet/ADB enable flags.

### Path 2: Jiritsu / ld.so.preload Library Injection
**Difficulty:** High | **Risk:** Medium-High | **Reversibility:** Partially

The ZV-E10 Camtool project demonstrated that on CXD90045-based cameras, code can be injected by:
1. Writing a custom shared library (.so) to a writable path
2. Using Jiritsu's file-write mechanism to place it where the dynamic linker loads it
3. The camera's imaging process loads the library via dlopen/ld.so.preload

**On the a6400:**
- `libJiritsu.so` is present, confirming the subsystem exists
- The dynamic linker is `ld-2.11.2.so` (glibc)
- Writable locations: `/tmp` (nflasha10), `/setting`, `/log`, `/etc`
- `/etc` is writable vfat on nflasha10 - this is where `ld.so.preload` could potentially be placed
- **Risks:** A bad library crashes the imaging process and reboots the camera. Repeated crashes of the same library name hit a cache. Recovery requires service shell access.
- **Next steps:** 
  1. Check if `/etc/ld.so.preload` exists or can be created
  2. Test with a minimal .so that logs to /tmp but does no harm
  3. If injection works, the loaded code could hook into the menu system

### Path 3: Hijacking an Existing Menu Entry
**Difficulty:** Very High | **Risk:** High | **Reversibility:** No (requires reflash)

The camera has some menu items that invoke shell scripts or external binaries:
- `WebApiLauncher.sh` - kills and restarts `orb-server` (the Sony remote control web API)
- `change_mode.sh` - switches camera modes
- Various `.elf` binaries called from menu operations

**Theory:** If a menu action triggers a script or binary that reads from a writable path, you could replace that file with your own code.

**On the a6400:**
- These scripts live in `/usr/bin/` which is **read-only**
- You cannot replace them without reflashing the /usr partition
- However, if any script sources a file from a writable location (like `/tmp` or `/etc`), that could be hijacked
- **Next step:** Decompile key scripts and trace their file dependencies. Look for any that read from writable paths.

### Path 4: Repurposing the Android Partition
**Difficulty:** Very High | **Risk:** Very High | **Reversibility:** Unknown

The a6400 has Android partitions (nflasha16 = `/android/system`, nflasha17 = `/android/data`) totaling 240MB. These appear vestigial.

**Theory:** If the boot process still tries to start Android services, and if the Android partition is writable or flashable, you could place an app there.

**Reality check:**
- The Android system partition is ext2 read-only
- The boot process likely skips Android initialization on non-PMCA cameras
- Even if Android starts, there's no UI bridge to the native menu
- **This path is speculative and high-risk. Not recommended.**

### Path 5: Custom Firmware with Modified Menu
**Difficulty:** Extreme | **Risk:** Extreme | **Reversibility:** Only via official firmware restore

Building a modified firmware that adds menu entries to the .uxc files and custom binaries.

**Blockers:**
- CXD90045 firmware is AES-encrypted with per-model keys (not publicly known)
- The firmware has integrity checks at three levels (CRC-32, per-file manifests, per-frame checksums)
- No one has published a full repack-and-flash workflow for CXD90045
- **This is not currently feasible.**

---

## Recommended Next Steps (in order of priority)

1. **Dump and analyze Backup.bin** - Via service shell, copy `/setting/Backup.bin` to SD card. Analyze for telnet/ADB/debug enable flags. This is the safest path to persistent network access.

2. **Map writable paths referenced by system scripts** - From the service shell, grep through all scripts in `/usr/bin/` and `/usr/sbin/` for references to `/tmp`, `/etc`, `/setting`, or other writable mounts. Any script that sources or reads from a writable path is a potential hook.

3. **Test /etc/ld.so.preload** - Check if this file exists. If not, try creating it with a path to a minimal test .so on `/tmp`. If the dynamic linker respects it, this is the code injection path.

4. **Catalog running processes** - From service shell, run `ps` and `cat /proc/*/cmdline` to understand what's running. Look for processes that watch file paths or respond to signals.

5. **Investigate /tmp persistence** - Since /tmp survives reboot, test whether files placed there are read by any boot process. Try placing a script named `autoexec.sh` or similar common names and reboot.

---

## Key Community Resources
- [Sony-PMCA-RE](https://github.com/ma1co/Sony-PMCA-RE) - The service shell tool (ma1co)
- [OpenMemories-Tweak](https://github.com/ma1co/OpenMemories-Tweak) - Settings modifier for PMCA cameras
- [CXD90045-FPSU](https://github.com/Lost-week-1025/CXD90045-FPSU) - CXD90045 firmware structure research
- [Sony-ZVE-10-Camtool](https://github.com/nolan2628/Sony-ZVE-10-Camtool) - Jiritsu dlopen payload on ZV-E10
- [sony-a7c2-research](https://github.com/ChambersXDU/sony-a7c2-research) - A7C II firmware research
- [Do Code That Matters](https://docodethatmatters.com/sony-a6000-a6400-research/) - a6000/a6400 research hub

## Architecture Summary
```
Camera Boot Sequence (conceptual):
  init (nflasha7, ro) 
    -> mount partitions
    -> load kernel modules  
    -> start imaging process (loads libObj.so, libJiritsu.so, etc.)
    -> start orb-server (web API, remote control)
    -> start bsa_server (Bluetooth)
    -> initialize menu (reads .uxc from /usr/share/app/)

Writable attack surface:
  /tmp (nflasha10)    - 12MB persistent, main staging area
  /etc (nflasha10)    - writable, potential ld.so.preload location
  /setting (nflasha2) - 20MB, Backup.bin lives here
  /log (nflasha11)    - 500MB, rarely checked
  /cert (nflasha12)   - 40MB, network certs
  /android/data (nflasha17) - 100MB, likely unused
```
