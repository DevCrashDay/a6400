# a6400 Homebrew Platform

**Unlock your Sony a6400's hidden potential.**

This project turns the Sony a6400 mirrorless camera into a programmable platform — adding features Sony never shipped, like unlimited video recording, hands-free astrophotography, automated focus stacking, and in-camera film emulation. Everything runs on the camera itself with no permanent modifications and a one-file killswitch to undo it all.

> **Project status (October 2026):** Boot persistence is confirmed working on firmware v2.00. The camera runs homebrew scripts automatically on every power-on. Shutter control, focus bracketing, and recording limit removal are all verified on real hardware.

---

## What Can It Do?

### Remove the 30-Minute Video Recording Limit

The a6400 artificially caps video clips at ~30 minutes (an old EU import duty rule). This project removes that limit, letting the camera record continuously for up to **13 hours** — limited only by your SD card and battery.

- Three-byte settings change using Sony's own built-in tool
- Takes effect immediately, no reboot needed
- Fully reversible with one command or a factory reset
- Same method used safely by [OpenMemories-Tweak](https://github.com/ma1co/OpenMemories-Tweak) on thousands of Sony cameras since 2016

### Astrophotography: Bulb Exposures & Intervalometer

Take ultra-long exposures and automated shooting sequences with no external remote or phone needed:

- **Bulb exposures of any length** — 2 minutes, 10 minutes, an hour — by script
- **Intervalometer** for star trails, time-lapses, and stacking
- Runs in the background so you can walk away from the camera
- Scheduling is clock-based, so timing doesn't drift over long runs

```sh
# Single 4-minute exposure
/tmp/shutter.sh bulb 240

# 30 frames of 2 minutes each for deep-sky stacking
/tmp/intervalometer.sh -b 120 -n 30 -i 125 --bg

# Star trails until you stop it
/tmp/intervalometer.sh -n 0 -i 22 --bg
```

### Focus Stacking / Bracketing

Automatically shoot a series of frames while stepping the focus between each shot — for macro, landscape, and product photography. Stack the results in Helicon Focus, Zerene Stacker, or Photoshop.

- **Confirmed working** on camera (2026-10-29, SEL18135 lens, 10-frame bracket)
- Adjustable step size (small/medium/large), direction (near/far), and frame count
- No phone, Wi-Fi, or tethering cable required

```sh
# 20 frames, small focus steps toward far
/tmp/focus_bracket.sh -n 20 -d far

# Larger steps for landscape depth
/tmp/focus_bracket.sh -n 10 -z large -d far
```

### In-Camera Film Emulation ("The Ritual")

An always-on film developing engine that watches for new photos and automatically creates film-emulated versions on your SD card:

| Film Stock | Look |
|---|---|
| **Portra 400** | Warm skin tones, gentle contrast, fine grain |
| **CineStill 800T** | Tungsten-balanced, halation glow, cinematic |
| **Fuji Provia 100F** | Vivid color, high saturation, fine detail |
| **Kodak Gold 200** | Classic warm consumer color, visible grain |
| **Tri-X 400** | Classic black & white, strong grain, rich tones |

Developed images appear in `/RITUAL/` on your SD card. Configure film stock, grain, and halation via `RITUAL.ini`.

### Boot Persistence

All of the above can start **automatically when you turn on the camera**. No computer needed after the one-time setup. A file on your SD card disables everything instantly if needed.

---

## What's Verified vs. What Isn't

Transparency matters. Here's exactly what has and hasn't been tested on real hardware:

### Confirmed Working (tested on a6400 fw v2.00)

- Service shell access via [Sony-PMCA-RE](https://github.com/ma1co/Sony-PMCA-RE)
- Native ARM code execution on camera
- Boot persistence — LD_PRELOAD hook survives cold power-cycle (verified 2026-10-27)
- Script-driven shutter: single shot, half-press, and 45-second bulb (verified 2026-10-29)
- Focus stepping and 10-frame focus bracket (verified 2026-10-29, SEL18135)
- 30-minute recording limit removal via Backup.bin properties
- Telnet daemon (port 2323, opt-in) and HTTP server (port 8080) at boot
- Film emulation daemon startup at boot

### Not Yet Tested on Camera

- Intervalometer multi-frame sequences (works in dry-run and emulation)
- Wi-Fi photo offload
- Focus bracket with lenses other than SEL18135
- Web darkroom UI over camera Wi-Fi

### Not Possible (confirmed dead ends)

| Limitation | Why |
|---|---|
| **PlayMemories Camera Apps** | Sony removed the app platform from all post-2018 cameras. There is no "Application List" menu on the a6400 and no way to install apps. |
| **On-screen GUI / menu modifications** | The display uses a proprietary DMP GPU (no `/dev/fb0`). Menu files are binary `.uxc` on a read-only partition with no repack tools. |
| **Custom firmware** | Firmware is AES-encrypted with unknown per-model keys and has three layers of integrity checks. No one has cracked CXD90045 firmware. |
| **exFAT SD cards** | The kernel (Linux 3.0.27, from 2019) doesn't support exFAT. Use FAT32. |
| **Real-time clock** | No RTC battery. The clock resets to January 1, 1970 every time the camera powers off. |

---

## Getting Started

### What You Need

- Sony a6400, firmware v2.00
- USB cable (USB-A to micro-USB)
- Windows PC
- [pmca-console](https://github.com/ma1co/Sony-PMCA-RE) (download `pmca-console-v0.18-win.exe`)
- SD card formatted as **FAT32** (not exFAT)

### Step 1: Enter the Service Shell

1. On the camera: **Menu → Setup → USB Connection → Mass Storage**
2. Connect camera to PC via USB
3. On PC, run:
   ```
   pmca-console-v0.18-win.exe serviceshell
   > shell
   ```
4. You now have a root shell on the camera

> **Heads up:** The service shell is a single continuous session. If the connection drops, you'll need to power-cycle the camera to reconnect. Keep a ~3 second pause between commands.

### Step 2: Pick Your Features

**Remove the 30-minute recording limit** (quickest win):
```sh
/usr/bin/bk.elf w 003c0373 0D
/usr/bin/bk.elf w 003c0374 01
/usr/bin/bk.elf w 003c0375 00
/usr/bin/bk.elf s
```
Done. Takes effect on the next recording. Undo with `--restore` (see [tools/README_rec_unlimit.md](tools/README_rec_unlimit.md)).

**Install the boot hook** (for all other features):
```sh
# Copy files from SD card to camera
cp /mnt/autoexec_hook.so /tmp/autoexec_hook.so
cp /mnt/autoexec.sh /tmp/autoexec.sh
chmod 755 /tmp/autoexec_hook.so /tmp/autoexec.sh

# Enable the hook
echo '/tmp/autoexec_hook.so' > /etc/ld.so.preload
```
After a power-cycle, the camera will automatically mount the SD card and start services.

**Test the shutter trigger** (no boot hook needed):
```sh
# Push scripts from PMCA prompt (before typing 'shell')
push astro\shutter.sh /tmp/shutter.sh
push astro\probe_shutter.sh /tmp/probe_shutter.sh

# In the shell
chmod 755 /tmp/shutter.sh /tmp/probe_shutter.sh
/tmp/probe_shutter.sh          # Takes one test photo
/tmp/shutter.sh bulb 10        # 10-second bulb (set dial to BULB first)
```

---

## Repository Contents

```
a6400/
├── README.md                       # This file
├── hook/
│   ├── autoexec_hook.so            # Boot persistence hook (patched .so with ARM constructor)
│   ├── autoexec.sh                 # Boot script — mounts SD, starts services
│   ├── build_autoexec_hook.py      # Rebuild the hook from source (needs Keystone Engine)
│   └── libAK8975.so               # Original unpatched library for reference
├── tools/
│   ├── rec_unlimit.sh              # Remove / restore the 30-min recording limit
│   └── README_rec_unlimit.md       # Full documentation for the recording limit tool
├── astro/
│   ├── shutter.sh                  # Script-driven shutter: single shot, bulb, half-press
│   ├── intervalometer.sh           # Automated interval shooting for astro / timelapse
│   ├── probe_shutter.sh            # First-run test script
│   └── README.md                   # Astro tools documentation
├── focus_bracket/
│   ├── focus_bracket.sh            # Automated focus stacking bracket
│   ├── focus.sh                    # Focus move functions (sourced by other scripts)
│   ├── shutter.sh                  # Shutter functions (shared with astro/)
│   ├── probe_focus.sh              # Focus move test (no pictures taken)
│   ├── README.md                   # Focus bracket documentation
│   └── FOCUS_STEP_RESEARCH.md      # How the focus key codes were reverse-engineered
├── check.sh                        # Full diagnostic — validates hook, clears state, tests
├── verify-after-boot.sh            # Confirms hook fired after a real power-cycle
├── a6400_storage_dump_guide.md     # Guide to dumping NAND partitions for research
├── research/
│   ├── README.md                   # Detailed feasibility analysis and architecture notes
│   ├── SYSTEM_INFO.md              # Hardware specs, partition table, key binaries
│   └── NEXT_STEPS_COMMANDS.md      # Service shell command reference
└── PROJECT_MEMORY.md               # Running project log
```

---

## How It Works (Technical Details)

### The Boot Hook

The a6400 runs Linux 3.0.27 on a quad-core ARM Cortex-A5 SoC (CXD90045). The standard Linux dynamic linker (`ld-2.11.2.so`) checks `/etc/ld.so.preload` on every process start and loads any shared libraries listed there.

We place a patched copy of `libAK8975.so` (an unused compass driver — the a6400 has no magnetometer) in `/tmp/autoexec_hook.so`. The patch adds a `DT_INIT` constructor that:

1. Checks for killswitch files
2. Grabs an atomic lock (`O_EXCL` on `/tmp/autoexec.lock`)
3. Double-forks so it doesn't block the host process
4. Runs `/tmp/autoexec.sh`, which mounts the SD card and starts everything

Since `/tmp` and `/etc` are on the same persistent flash partition (nflasha10), the hook and its configuration survive reboots. The `autoexec.sh` script clears the lock file on exit (`trap`) to prevent boot-lockout.

### Shutter & Focus Control

Sony's camera firmware has an internal message queue (`0x007A018C`) that accepts virtual key events — the same mechanism the Wi-Fi remote control app uses. The on-camera tool `sndcmd.elf` can send messages to this queue:

```sh
# Press the shutter button (S2 = full press)
sndcmd.elf --sync 0x007A018C d:2 d:0x1A00000E d:0 d:1 d:0   # press
sndcmd.elf --sync 0x007A018C d:2 d:0x1A00000E d:0 d:0 d:0   # release
```

Focus uses key `0x1A000025` (the same Near/Far command the USB PC remote sends), with a signed step size: positive = far, negative = near. Sony uses sizes 1, 3, and 7.

### Recording Limit

The limit is stored in three single-byte properties in the camera's settings database (Backup.bin). The built-in `bk.elf` tool reads and writes these properties. The firmware checks the limit at recording start, converting it to 90 KHz timer ticks. The "unrestricted" value (13h 01m) comes from Sony's own camcorder firmware and stays safely below the 32-bit timer overflow at 13h 15m 21s.

---

## Hardware Overview

| Component | Detail |
|---|---|
| **Camera** | Sony ILCE-6400, firmware v2.00 |
| **SoC** | CXD90045 ("Astra"), BIONZ X image processor |
| **CPU** | 4x ARM Cortex-A5 rev 1, ~351 BogoMIPS each |
| **RAM** | ~1.25 GB (400–600 MB free while shooting) |
| **OS** | Linux 3.0.27 (SMP PREEMPT RT), BusyBox 1.13.4, glibc 2.11.2 |
| **Storage** | ~1.2 GB internal NAND flash across 20+ partitions |

Key partitions: `/tmp` and `/etc` share a 12 MB writable flash partition (nflasha10) that persists across reboots. `/usr` (300 MB, apps and libraries) and `/` (8 MB, root filesystem) are read-only ext2. The full partition map is in [research/SYSTEM_INFO.md](research/SYSTEM_INFO.md).

---

## Safety & Recovery

This project is designed to be safe and reversible:

- **No read-only partitions are modified.** Everything lives on writable flash (`/tmp`, `/etc`) and the SD card.
- **No firmware is flashed.** The camera's firmware is untouched.
- **No safety mechanisms are disabled.** Thermal shutdown, battery protection, and all camera safeguards remain active.

### Emergency Killswitch

If anything goes wrong, put a file named **`disable_hook`** on your SD card (any empty file will do). On the next boot, the hook disables itself, removes `/etc/ld.so.preload`, and the camera returns to stock behavior.

### Full Uninstall

From the service shell:
```sh
rm -f /etc/ld.so.preload /tmp/autoexec_hook.so /tmp/autoexec.sh /tmp/autoexec.lock
```

### Nuclear Option

A [Sony firmware reinstall](https://www.sony.com/en/electronics/support/mirrorless-interchangeable-lens-cameras-e-mount-body/ilce-6400/downloads) restores every partition to factory state. This is extremely unlikely to be needed — the hook only touches a non-critical library and writable partitions.

---

## Scripting Gotchas

If you're writing your own scripts for the camera:

- **LF line endings only.** CRLF breaks everything — the shebang, `sleep`, redirects. Use `dos2unix` or configure your editor.
- **BusyBox 1.13.4 is ancient.** Missing: `head`, `od`, `hexdump`, `killall`, `nice`, `awk`, `wc`, `id`, `stat`, `getopts`, `printf`. Also `cat` has no `-v` flag. Stick to: `cat`, `dd`, `grep`, `ls`, `echo`, `sleep`, `rm`, `kill`, `pidof`, `chmod`, `mount`, `sh`, `mkdir`, `cp`, `mv`, `df`, `ps`, `tr`, `sed`, `test`/`[`.
- **`/tmp` is not temporary.** It's persistent flash. Stale processes, logs, and lock files survive between sessions. Clean up before testing.
- **`sleep` only takes integers.** No `sleep 0.5`.
- **`sndcmd.elf` takes ~1 second per call.** Budget this into timing-sensitive scripts.
- **PMCA `pull` truncates by ~32 KB.** To get large files off the camera, split them with `dd` and reassemble on PC.

---

## Community & Resources

This project builds on the work of the Sony homebrew research community:

- [Sony-PMCA-RE](https://github.com/ma1co/Sony-PMCA-RE) — The service shell tool that makes all of this possible (ma1co)
- [OpenMemories-Tweak](https://github.com/ma1co/OpenMemories-Tweak) — Settings modifier for older Sony cameras
- [Sony-ZVE-10-Camtool](https://github.com/nolan2628/Sony-ZVE-10-Camtool) — Jiritsu/dlopen injection on the ZV-E10
- [CXD90045-FPSU](https://github.com/Lost-week-1025/CXD90045-FPSU) — CXD90045 firmware structure research

---

## License

This project is for educational and personal homebrew research. Sony, BIONZ, and related trademarks belong to Sony Corporation. This project is not affiliated with or endorsed by Sony.
