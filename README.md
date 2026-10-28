# a6400 Homebrew Platform

Homebrew research and tools for the **Sony ILCE-6400 (a6400)**, firmware v2.00.

This project documents what is and isn't possible on the a6400 through its service shell, including a working boot-persistence hook, diagnostic scripts, and in-camera application framework.

> **Status:** Boot persistence is **confirmed working** as of 2026-10-27. The LD_PRELOAD hook survives a genuine cold power-cycle and launches the full homebrew stack automatically.

---

## Table of Contents

- [What's Confirmed Working](#whats-confirmed-working)
- [What Isn't Possible](#what-isnt-possible)
- [Hardware Overview](#hardware-overview)
- [Getting Started](#getting-started)
- [Repository Contents](#repository-contents)
- [Boot Persistence (LD_PRELOAD Hook)](#boot-persistence-ldso_preload-hook)
- [The Ritual — In-Camera Film Lab](#the-ritual--in-camera-film-lab)
- [Diagnostic Scripts](#diagnostic-scripts)
- [Known Limitations & Quirks](#known-limitations--quirks)
- [Research & Further Reading](#research--further-reading)
- [Safety & Recovery](#safety--recovery)
- [License](#license)

---

## What's Confirmed Working

These have been tested and verified on a real a6400 running firmware v2.00:

| Capability | Status | Notes |
|---|---|---|
| Service shell access | **Verified** | Root shell via [Sony-PMCA-RE](https://github.com/ma1co/Sony-PMCA-RE) `serviceshell` |
| Native ARM code execution | **Verified** | Static ARMv7 ELFs run from `/tmp` |
| Boot persistence (LD_PRELOAD) | **Verified** | Hook fires on cold power-cycle, launches full stack |
| Telnet daemon (port 2323) | **Verified** | Opt-in via `/mnt/enable_telnet` flag file on SD card |
| HTTP server (port 8080) | **Verified** | BusyBox httpd serving web UI over camera Wi-Fi |
| In-camera film processing | **Verified** | `ritual_daemon` auto-develops JPEGs with film emulations |
| SD card mount from userspace | **Verified** | `mount -t vfat /dev/mmca1 /mnt` (FAT32 only) |
| Writable `/etc` and `/tmp` | **Verified** | Both are persistent vfat on nflasha10 |
| PTP/IP camera control | **Verified** | localhost:15740 when "Ctrl w/ Smartphone" is enabled |

## What Isn't Possible

These are **confirmed dead ends** — don't waste time on them:

| Limitation | Reason |
|---|---|
| **PlayMemories Camera Apps** | Sony removed the PMCA platform from all post-2018 cameras. No "Application List" menu exists on the a6400. `pmca-console install` will not work. |
| **OpenMemories-Tweak** | Requires PMCA app install, which is not supported. |
| **Direct menu/UI modification** | `/usr` is read-only ext2. The `.uxc` menu files are binary, with no known repack tools. |
| **Framebuffer access** | No `/dev/fb0`. Display is driven by the DMP SUGILITE GPU via `/dev/dmpgles2` and Guiengine_161H. |
| **Custom firmware flashing** | CXD90045 firmware is AES-encrypted with per-model keys (not publicly known). Three-level integrity checks. No repack tooling exists. |
| **exFAT SD cards** | The kernel (3.0.27) has no exFAT support. SD cards must be FAT32. |
| **Real-time clock** | No RTC battery. Clock resets to `1970-01-01 00:00:00` on every power-off. |
| **Large file transfer via PMCA** | `pmca-console pull` truncates every file by ~32 KB. Workaround: split with `dd` into overlapping chunks and reassemble on PC. |

---

## Hardware Overview

| Component | Detail |
|---|---|
| **Model** | Sony ILCE-6400 |
| **SoC** | CXD90045 ("Astra"), BIONZ X |
| **CPU** | 4x ARM Cortex-A5 rev 1, ~351 BogoMIPS each |
| **RAM** | ~1.25 GB physical (400-600 MB free during shooting) |
| **Kernel** | Linux 3.0.27_nl-rt106+ (SMP PREEMPT RT, built 2019-05-27) |
| **Shell** | BusyBox v1.13.4 (2017-04-15), ash |
| **glibc** | 2.11.2 |
| **NAND** | Multiple partitions on internal flash, ~1.2 GB total |

### Partition Map

| Partition | Mount | Type | Size | Writable | Purpose |
|---|---|---|---|---|---|
| nflasha2 | `/setting` | vfat | 20 MB | Yes | Camera settings, Backup.bin |
| nflasha3 | `/system` | vfat | 48 MB | Yes | System data |
| nflasha5 | `/wbi` | wbi | 60 MB | No | Warm Boot Image |
| nflasha6 | (none) | vfat | 16 MB | No | Jiritsu subsystem |
| nflasha7 | `/` | ext2 | 8 MB | **No** | Root filesystem |
| nflasha10 | `/tmp` + `/etc` | vfat | 12 MB | **Yes** | Work partition — persistent across reboots |
| nflasha11 | `/log` | vfat | 500 MB | Yes | Log storage |
| nflasha12 | `/cert` | vfat | 40 MB | Yes | Network certificates |
| nflasha15 | `/usr` | ext2 | 300 MB | **No** | Applications, libraries, UI resources |
| nflasha16 | `/android/system` | ext2 | 140 MB | No | Android (vestigial, unused) |
| nflasha17 | `/android/data` | vfat | 100 MB | Yes | Android data (vestigial) |
| nflasha18 | `/lens` | vfat | 20 MB | Yes | Lens calibration |

**Critical detail:** `/tmp` and `/etc` are the **same persistent vfat partition** (nflasha10). Nothing on this camera is tmpfs. Files in `/tmp` and `/etc` survive reboots. This is what makes the LD_PRELOAD hook persistent — but it also means stale lock files and logs persist.

---

## Getting Started

### Prerequisites

- Sony a6400 with firmware **v2.00**
- USB cable (USB-A to micro-USB)
- Windows PC (for PMCA service shell)
- [Sony-PMCA-RE](https://github.com/ma1co/Sony-PMCA-RE) — download `pmca-console-v0.18-win.exe`
- SD card formatted as **FAT32** (not exFAT)

### Entering the Service Shell

1. Set camera USB mode: **Menu → Setup → USB Connection → Mass Storage**
2. Connect camera to PC via USB
3. Run:
   ```
   pmca-console-v0.18-win.exe serviceshell
   > shell
   ```
4. You now have a root shell (BusyBox ash)

> **Important:** The service shell runs as a single continuous session. If you disconnect or the tool crashes, the camera drops off USB and requires a full power-cycle to reconnect. Keep ~3 second delays between commands to avoid overwhelming the shell.

---

## Repository Contents

```
a6400/
├── README.md                    # This file
├── PROJECT_MEMORY.md            # Running project log
├── hook/
│   ├── autoexec_hook.so         # The LD_PRELOAD boot hook (patched libAK8975.so)
│   ├── autoexec.sh              # Boot script launched by the hook
│   ├── build_autoexec_hook.py   # Python script to rebuild autoexec_hook.so
│   └── libAK8975.so             # Original (unpatched) library for reference
├── check.sh                     # Full diagnostic script — tests hook, clears state, fires hook
├── verify-after-boot.sh         # Post-power-cycle verification script
├── a6400_storage_dump_guide.md  # Guide to dumping NAND partitions
└── research/
    ├── README.md                # Detailed homebrew research (paths, architecture, GUI analysis)
    ├── SYSTEM_INFO.md           # Hardware and software details from live camera
    └── NEXT_STEPS_COMMANDS.md   # Service shell command reference
```

---

## Boot Persistence (LD_PRELOAD Hook)

The hook makes homebrew code run automatically every time the camera powers on, without entering the service shell.

### How It Works

1. **`/etc/ld.so.preload`** contains the path `/tmp/autoexec_hook.so`
2. The Linux dynamic linker (`ld-2.11.2.so`) loads `autoexec_hook.so` into every process at startup
3. `autoexec_hook.so` is a patched copy of `libAK8975.so` (the AKM compass driver — dead code on the a6400, which has no magnetometer). A `DT_INIT` constructor is injected at offset 0x988.
4. The constructor:
   - Checks for killswitch files (`/tmp/disable_hook`, `/mnt/disable_hook`, `/mnt/disable.txt`)
   - Acquires an atomic lock via `O_EXCL` on `/tmp/autoexec.lock`
   - Double-forks (to not block the host process)
   - `execve`s `/bin/sh /tmp/autoexec.sh`
5. **`autoexec.sh`** then:
   - Clears the lock on exit via `trap` (critical — `/tmp` is persistent flash, a leftover lock would prevent the next boot)
   - Mounts the SD card
   - Checks the killswitch again (SD card isn't mounted at early boot, so the hook can't check it)
   - Copies binaries from SD to `/tmp` and starts services (telnet, httpd, ritual_daemon, watchdog)

### Installing the Hook

From the service shell:

```sh
# 1. Copy hook files to camera (from SD card at /mnt)
cp /mnt/autoexec_hook.so /tmp/autoexec_hook.so
cp /mnt/autoexec.sh /tmp/autoexec.sh
chmod 755 /tmp/autoexec_hook.so /tmp/autoexec.sh

# 2. Install the preload entry
echo '/tmp/autoexec_hook.so' > /etc/ld.so.preload

# 3. Verify
cat /etc/ld.so.preload
```

### Killswitch (Emergency Disable)

If anything goes wrong, create any of these files on the SD card to disable the hook on next boot:

- `/mnt/disable_hook`
- `/mnt/disable.txt`
- `/tmp/disable_hook` (via service shell)

The hook will remove `/etc/ld.so.preload` and exit cleanly. The camera returns to stock behavior.

### Rebuilding the Hook

Requires Python 3 and [Keystone Engine](https://www.keystone-engine.org/):

```sh
pip install keystone-engine
python build_autoexec_hook.py
```

This patches `libAK8975.so` to produce `autoexec_hook.so` by:
- Writing ARM assembly at offset 0x988 (the `AKSC_InitDecomp8975` export)
- Inserting a `DT_INIT` entry in the ELF dynamic section pointing to 0x988

---

## The Ritual — In-Camera Film Lab

"The Ritual" is an in-camera film developing engine that watches for new photos and automatically processes them with analog film stock emulations.

### Film Stocks

| Stock | Character |
|---|---|
| **Portra 400** | Warm skin tones, gentle contrast, fine grain |
| **CineStill 800T** | Tungsten-balanced, halation glow, cinematic |
| **Fuji Provia 100F** | Vivid color, high saturation, fine detail |
| **Kodak Gold 200** | Classic warm consumer color, visible grain |
| **Tri-X 400** | Classic B&W, strong grain, rich tonal range |

### How It Works

- `ritual_daemon` monitors `DCIM/` on the SD card for new JPEG (and ARW) files
- Applies Hurter-Driffield S-curve tone mapping per film stock
- Adds density-dependent film grain simulation
- Outputs developed images to `/mnt/RITUAL/` on the SD card
- Configuration via `/mnt/RITUAL.ini` (stock, grain level, halation on/off)

### Additional Camera Tools

| Tool | Purpose |
|---|---|
| `ritual_menu` | Interactive terminal UI for cycling film stocks and settings |
| `arw_develop` | Full Sony ARW raw developer (white balance, demosaicing) |
| `sony_ptpip` | PTP/IP camera control (shutter, focus, EV, bulb) via localhost:15740 |
| `star_trails` | Lighten-blend compositor for star trail photography |
| `timelapse_mux` | MJPEG AVI muxer for timelapse sequences |
| `intervalometer.sh` | Interval timer shooting script |
| `focus_bracket.sh` | Focus bracketing automation |
| `aeb_hdr.sh` | Auto exposure bracketing for HDR |
| `wifi_offload.sh` | FTP upload of processed photos |
| `watchdog.sh` | Runtime supervisor (thermal, battery, killswitch monitoring) |
| `probe.sh` | Hardware and access diagnostic probe |

---

## Diagnostic Scripts

### `check.sh` — Full Diagnostic

Run from the service shell to test the entire hook chain:

```sh
chmod 755 /mnt/check.sh
/bin/sh /mnt/check.sh
```

This script:
1. Verifies mount points and persistent flash
2. Checks hook file presence and integrity (ELF signature, file size)
3. Reads `/etc/ld.so.preload`
4. Clears stale state (old locks, PIDs, daemons from prior sessions)
5. Fires the hook via `LD_PRELOAD=/tmp/autoexec_hook.so /bin/true`
6. Checks whether autoexec.sh launched and produced logs

### `verify-after-boot.sh` — Boot Persistence Verifier

Run **after a real camera power-cycle** to confirm the hook fired on its own:

```sh
# BEFORE power-cycling, clear old state:
rm -f /tmp/autoexec.log /tmp/autoexec.lock /tmp/autoexec.pid /tmp/ritual.log

# Power OFF camera completely, then power ON
# Re-enter service shell, then run:
/bin/sh /mnt/verify-after-boot.sh
```

**How to tell it really worked:** The a6400 has no RTC — a genuine cold boot resets the clock to `1970-01-01 00:00:00`. If `autoexec.log` is timestamped near `00:00:0X` (seconds of uptime), the hook fired at boot. If the timestamp continues a prior session's clock, the camera didn't actually reboot.

---

## Known Limitations & Quirks

### BusyBox 1.13.4

The on-camera shell is extremely minimal. **Missing applets** — do not use these in scripts:

`head`, `od`, `hexdump`, `killall`, `nice`, `awk`, `wc`, `id`, `stat`

**Also:** `cat` has no `-v` flag.

**Available and tested:** `cat`, `dd`, `grep`, `ls`, `echo`, `sleep`, `rm`, `kill`, `pidof`, `chmod`, `mount`, `sh`, `mkdir`, `cp`, `mv`, `df`, `ps`, `tr`, `sed`, `test`/`[`

### Line Endings

All scripts pushed to the camera **must use LF (Unix) line endings**. CRLF will break the shebang (`not foundsh:` error), corrupt `sleep` arguments, and cause `bad fd number` on redirects.

### No Volatile Filesystem

There is **no tmpfs** anywhere. `/tmp` is persistent flash (nflasha10). This means:
- Stale daemons, logs, and lock files survive between service shell sessions
- Diagnostic scripts should kill old processes and remove old files before testing
- The `autoexec.sh` lock trap (`trap 'rm -f /tmp/autoexec.lock' EXIT`) is critical to prevent boot-lockout

### PMCA File Transfer Truncation

`pmca-console pull` truncates files by ~32 KB. To transfer large files off the camera, split them using `dd`:

```sh
dd if=/dev/nflashaX of=/mnt/chunk_00 bs=896k count=1 skip=0
dd if=/dev/nflashaX of=/mnt/chunk_01 bs=896k count=1 skip=1
# ... reassemble on PC
```

### Service Shell Session

- One continuous session per USB connection — disconnecting requires a power-cycle
- Add ~3 second delays between commands
- The camera can become unresponsive if commands are sent too quickly

---

## Research & Further Reading

Detailed research notes are in the [`research/`](research/) directory:

- **[research/README.md](research/README.md)** — Full homebrew feasibility analysis, GUI architecture, injection vectors, and ranked paths to camera-GUI homebrew
- **[research/SYSTEM_INFO.md](research/SYSTEM_INFO.md)** — Complete hardware specs, partition table, key libraries and binaries
- **[a6400_storage_dump_guide.md](a6400_storage_dump_guide.md)** — Step-by-step NAND partition dumping guide

### Key External Resources

- [Sony-PMCA-RE](https://github.com/ma1co/Sony-PMCA-RE) — Service shell tool (ma1co)
- [OpenMemories-Tweak](https://github.com/ma1co/OpenMemories-Tweak) — Settings modifier (PMCA cameras only)
- [Sony-ZVE-10-Camtool](https://github.com/nolan2628/Sony-ZVE-10-Camtool) — Jiritsu/dlopen on ZV-E10
- [CXD90045-FPSU](https://github.com/Lost-week-1025/CXD90045-FPSU) — CXD90045 firmware structure

### Injection Vectors (for researchers)

| Vector | Status | Notes |
|---|---|---|
| `/etc/ld.so.preload` | **Working** | Current boot persistence mechanism |
| `/setting/env.txt` with `LD_PRELOAD` | Untested | Init reads this file; env.txt not present by default |
| Backup.bin settings flags | Untested | ~15,000 settings entries; may contain telnet/ADB enable flags |
| libJiritsu.so dlopen | Untested | Present in `/usr/lib/`; used on ZV-E10 for injection |
| Android partition repurposing | Speculative | nflasha16/17 are vestigial; boot process likely skips Android init |

---

## Safety & Recovery

This project follows a **zero NAND writes** policy for the read-only partitions. All homebrew runs from the writable `/tmp` and SD card.

### If Something Goes Wrong

1. **Killswitch:** Put a file named `disable_hook` or `disable.txt` on the SD card root. Next boot, the hook disables itself and removes `/etc/ld.so.preload`.

2. **Service shell recovery:** Connect via USB in Mass Storage mode and run:
   ```sh
   rm -f /etc/ld.so.preload /tmp/autoexec_hook.so /tmp/autoexec.sh /tmp/autoexec.lock
   ```

3. **Hard recovery:** If the camera won't boot (extremely unlikely — the hook only adds a `DT_INIT` to a non-critical library), a [Sony firmware reinstall](https://www.sony.com/en/electronics/support/mirrorless-interchangeable-lens-cameras-e-mount-body/ilce-6400/downloads) will restore all stock partitions.

### What This Project Does NOT Do

- Does not modify read-only partitions (`/`, `/usr`)
- Does not flash custom firmware
- Does not disable any camera safety mechanisms
- Does not void warranty any more than using the documented service shell does

---

## License

This project is for educational and personal homebrew research purposes. Sony, BIONZ, and related trademarks belong to Sony Corporation. This project is not affiliated with or endorsed by Sony.

The hook mechanism builds on research from the [Sony-PMCA-RE](https://github.com/ma1co/Sony-PMCA-RE) community.
