# a6400 System Information (Live Camera)
**Captured:** 2026-10-26 via PMCA service shell

## Hardware
- **Model:** ILCE-6400 (Sony a6400)
- **Firmware:** v2.00
- **SoC:** CXD90045 (ARM-CXD900X0), codename "Astra"
- **CPU:** 4x ARM Cortex-A5 rev 1, ~351 BogoMIPS each
- **Platform:** BIONZ X

## Software
- **Kernel:** Linux 3.0.27_nl-rt106+ (SMP PREEMPT RT)
- **Built:** 2019-05-27
- **Shell:** BusyBox v1.13.4 (2017-04-15), ash
- **glibc:** 2.11.2 (ld-2.11.2.so)
- **Working dir on shell entry:** /usr/share/app

## Partition Table
| Device | Mount | Type | Size | RW | Purpose |
|--------|-------|------|------|----|---------|
| nflasha1 | (none) | vfat | 8MB | - | Updater system |
| nflasha2 | /setting | vfat | 20MB | rw | Camera settings, Backup.bin |
| nflasha3 | /system | vfat | 48MB | rw | System main |
| nflasha4 | (none) | raw | 12MB | - | MPR Flash Backup |
| nflasha5 | /wbi | wbi | 60MB | - | Warm Boot Image |
| nflasha6 | (none) | vfat | 16MB | - | Jiritsu |
| nflasha7 | / (rootfs) | ext2 | 8MB | **ro** | Root filesystem |
| nflasha10 | /tmp | vfat | 12MB | **rw** | Work (persistent!) |
| nflasha11 | /log | vfat | 500MB | rw | Logs |
| nflasha12 | /cert | vfat | 40MB | rw | Network certificates |
| nflasha13 | (none) | vfat | 4MB | - | Updater work |
| nflasha15 | /usr | ext2 | 300MB | **ro** | Apps, libs, UI resources |
| nflasha16 | /android/system | ext2 | 140MB | ro | Android (vestigial) |
| nflasha17 | /android/data | vfat | 100MB | rw | Android data (vestigial) |
| nflasha18 | /lens | vfat | 20MB | rw | Lens data |
| nflasha23 | /PMBP | prfile | 2MB | - | PlayMemories info |

## Key Libraries in /usr/lib/
- libJiritsu.so (Jiritsu subsystem - boot/init related)
- libObj.so (UI object/view framework - the menu system)
- libMWF*.so (middleware framework)
- libmintx*.so (core system)

## Key Binaries in /usr/bin/
- orb-server (remote control web API, port 10000)
- bsa_server (Bluetooth)
- launch_shell.elf (init shell launcher, forks /bin/ash)
- scenario.elf (scenario execution)
- change_mode.sh, WebApiLauncher.sh (mode switching, web API)

## Key Directories
- /usr/share/app/ - UI resources (.uxc view files, .xdb, fonts, strings)
- /usr/share/network/ - Network configs (remote control, DLNA, MTP)
- /usr/scenario/ - Scenario .so files (AVBB_SCN_*, CAMERA_SCN_*, MPR_SCN_*)
- /usr/share/startbinary/ - logo.bin (boot logo)

## Access Method
```
pmca-console-v0.18-win.exe serviceshell
> shell
```
Camera USB mode must be set to "Mass Storage" in camera menu.
