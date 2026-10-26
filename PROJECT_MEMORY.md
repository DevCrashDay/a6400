# a6400 Project Memory

## Current State
- Service shell access confirmed working (PMCA v0.18, Mass Storage USB mode)
- Native ARMv7 ELF execution confirmed on camera (/tmp)
- Firmware v2.00 extracted and analyzed
- Research complete: the a6400 does NOT support PlayMemories Camera Apps (Sony removed PMCA from post-2018 cameras)
- Five paths to GUI-launched homebrew identified, ranked by feasibility
- Handoff v3 created with full research write-up

## Decisions
- 2026-10-26: Focus on Backup.bin analysis and ld.so.preload testing as most promising paths to persistent/GUI homebrew, rather than firmware modification (which is blocked by encryption)

## Done
- 2026-10-26: Confirmed service shell access via pmca-console serviceshell
- 2026-10-26: Ran custom hello_armv7.elf on camera, confirmed native code execution
- 2026-10-26: Mapped full partition layout and mount points from live camera
- 2026-10-26: Researched PlayMemories/PMCA/OpenMemories ecosystem - confirmed a6400 lacks app support
- 2026-10-26: Found libJiritsu.so and libObj.so present in a6400 firmware
- 2026-10-26: Found vestigial Android partitions (nflasha16/17, 240MB total)
- 2026-10-26: Identified CXD90045-FPSU research (Lost-week-1025) covering same SoC family
- 2026-10-26: Identified ZVE-10-Camtool Jiritsu/dlopen injection technique potentially applicable
- 2026-10-26: Created handoff6400_v3.zip with research write-up, system info, and next-steps commands

## What Worked
- 2026-10-26: pmca-console-v0.18-win.exe serviceshell gives root shell. USB must be Mass Storage mode.
- 2026-10-26: Static ARMv7 ELF with raw syscalls runs on camera from /tmp
- 2026-10-26: /tmp (nflasha10) is persistent vfat, survives reboot - good staging area

## What Didn't Work
- 2026-10-26: PlayMemories Camera Apps (PMCA install) - not supported on a6400, Sony removed the platform
- 2026-10-26: OpenMemories-Tweak - requires PMCA app install, cannot be used on a6400 directly
- 2026-10-26: Direct menu modification - /usr is read-only ext2, .uxc files are binary format, no repack tooling exists

## Findings
- 2026-10-26: PMCA `pull` command truncates every file by exactly 32 KB (8 blocks of 4096). Max reliable transfer per pull is 884,736 bytes (864 KB). Chunking with dd count=224 skip+=216 compensates but doesn't scale for large partitions.
- 2026-10-26: SD card shows in /proc/partitions as mmca10/mmca10p1 (major 250, minor 10/11, ~119 GB) but no /dev node exists. Must create with `mknod /tmp/sd b 250 11`. Card is exFAT which the Linux 3.0.27 kernel can't mount — needs FAT32 formatting.
- 2026-10-26: a6400 uses CXD90045 SoC, same as ZV-E10 and ZV-1F. Research from those cameras may transfer.
- 2026-10-26: libJiritsu.so present at /usr/lib/ - the Jiritsu subsystem used for code injection on ZV-E10 exists here
- 2026-10-26: /etc is writable vfat (same partition as /tmp, nflasha10) - potential location for ld.so.preload
- 2026-10-26: Firmware encryption uses AES-128-ECB (first 512B) + AES-256-CBC with per-model keys. Keys not public. Blocks custom firmware path.
- 2026-10-26: Camera has ~15,000 settings in Backup.bin (/setting/). May contain telnet/ADB enable flags from PMCA era.
- 2026-10-26: nflasha6 (16MB, "Jiritsu" partition) is where Jiritsu payloads may be staged
- 2026-10-26: UI views defined in .uxc binary files, menu logic in libObj.so, uses "belt" directional navigation model

## Open Issues
- Backup.bin extracted (1,148,036 bytes, SHA256: 9303ca57...ec16). Starts with 4-byte entries with R@/A markers. Need to decode setting ID+value format and search for telnet/ADB/debug enable flags.
- Need to test if /etc/ld.so.preload works on this camera
- Need to map which running processes load from writable paths
- Need to check if /tmp/autoexec.sh or similar is read at boot
- Need to explore what's actually on the Android partitions (likely empty/vestigial)
- CXD90045-FPSU research mentions a PC-side touch-to-direction converter for menu injection, but it hasn't been tested on hardware

## Session Log
- 2026-10-26: Thread "Launch homebrew apps from camera menu" - Researched all known paths to GUI-launched homebrew on the a6400. Created handoff6400_v3.zip with full write-up. Key finding: PMCA apps are not available, but Jiritsu/ld.so.preload injection and Backup.bin manipulation are promising alternatives.
- 2026-10-26: Thread "Dump a6400 storage for offline research" - Created a6400_storage_dump_guide.md with full read-only dump procedure covering raw block-device dumps and filesystem pulls for all 16 non-zero NAND partitions (~1.2 GB total). Pushed to repo.
