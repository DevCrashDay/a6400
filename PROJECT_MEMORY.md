# a6400 Project Memory

## Current State
- **Full homebrew platform built and operational locally** in F:\a6400project
- Boot persistence achieved via LD_PRELOAD hook: `autoexec_hook.so` is a patched `libAK8975.so` with ARM assembly constructor that forks `/bin/sh /tmp/autoexec.sh` at boot, with atomic lock and triple killswitch checks
- `autoexec.sh` boot sequence: mount SD, killswitch check, copy binaries to /tmp, start telnetd (port 2323), httpd (port 8080), ritual_daemon, and watchdog
- "The Ritual" in-camera film lab: `ritual_daemon` (static ARMv7 C binary) watches DCIM/ and auto-develops photos with analog film emulation (Portra 400, CineStill 800T, Fuji Provia, Kodak Gold, Tri-X 400) into /mnt/RITUAL/ on the SD card. Uses stb_image for JPEG decode, custom Hurter-Driffield S-curves, density-dependent film grain, and embedded JPEG extraction from ARW files.
- `ritual_menu` — interactive terminal menu for cycling film stocks/grain/halation settings on camera
- Mobile web darkroom UI at `http://192.168.122.1:8080/` via BusyBox httpd serving `www/` directory
- Desktop GUI setup wizard: `a6400_Homebrew_Wizard.py` (Tkinter) — auto-detects removable drives, assists with Zadig driver setup, deploys assets to SD, configures film presets, has killswitch toggle
- Full SDK with 5 example projects (hello_arm, hardware_info, button_logger, preload_hook, custom_film_stock), `camera_defs.h` with button codes and IPC structs
- Additional tools built: `arw_develop` (full RAW developer with WB/demosaic), `sony_ptpip` (PTP/IP localhost camera control), `star_trails` (lighten-blend compositor), `timelapse_mux` (MJPEG AVI assembler)
- Shell-based tools: `probe.sh` (hardware/access probe), `watchdog.sh` (supervisor/killswitch monitor), `wifi_offload.sh`, `intervalometer.sh`, `focus_bracket.sh`, `aeb_hdr.sh`
- Full partition dump completed: DUMP/ folder has 22 raw images of all NAND partitions (nflasha1 through nflasha23, plus variants like _setting, _wbi, _jiritsu, _work, _lens)
- Comprehensive documentation: README.md, CAPABILITIES.md, DEVELOPER_GUIDE.md, DRIVER_SETUP_GUIDE.md, USER_GUIDE.md, TROUBLESHOOTING.md
- Script-driven shutter (bulb > 30 s, intervalometer) implemented in `astro/` via Sony's InfraInput test hook (`sndcmd.elf --sync 0x007A018C`); awaiting first on-camera test (`astro/probe_shutter.sh half`). Next step: run astro/README.md test steps 5-7.

## Decisions
- 2026-10-26: Focus on Backup.bin analysis and ld.so.preload testing as most promising paths to persistent/GUI homebrew, rather than firmware modification (which is blocked by encryption)
- 2026-10-26: LD_PRELOAD via patched libAK8975.so chosen as boot persistence mechanism. Constructor uses ARM syscalls directly (sys_access, sys_open O_EXCL atomic lock, sys_fork double-fork, sys_execve) to launch autoexec.sh
- 2026-10-26: Safety-first design: triple killswitch (SD card `/mnt/disable_hook`, `/mnt/disable.txt`, `/tmp/disable_hook`), zero NAND writes policy, all homebrew runs at nice -n 19
- 2026-10-28: Shutter control uses key injection (virtual Wi-Fi-remote S1/S2 keys 0x1A00000D/0x1A00000E through InfraInput test cmd 2) instead of PTP/IP or the Remote API, because it needs no Wi-Fi session, no HTTP client and uses only stock binaries. Remote API on :10000 kept as fallback.

## Done
- 2026-10-26: Confirmed service shell access via pmca-console serviceshell
- 2026-10-26: Ran custom hello_armv7.elf on camera, confirmed native code execution
- 2026-10-26: Mapped full partition layout and mount points from live camera
- 2026-10-26: Researched PlayMemories/PMCA/OpenMemories ecosystem - confirmed a6400 lacks app support
- 2026-10-26: Found libJiritsu.so and libObj.so present in a6400 firmware
- 2026-10-26: Created handoff6400_v3.zip with research write-up, system info, and next-steps commands
- 2026-10-26: Built autoexec_hook.so — LD_PRELOAD boot persistence hook (build_autoexec_hook.py patches libAK8975.so using Keystone assembler, injects DT_INIT at 0xac10 pointing to ARM constructor at 0x988)
- 2026-10-26: Built ritual_daemon.c — in-camera film developing engine with 5 film stocks, stb_image-based JPEG/ARW processing, Hurter-Driffield tone curves, density-dependent grain
- 2026-10-26: Built ritual_menu.c — interactive terminal film stock selector with ANSI color UI
- 2026-10-26: Built arw_develop.c — full Sony ARW raw developer with TIFF/IFD parsing, white balance, demosaicing, bounded to <60MB RAM
- 2026-10-26: Built sony_ptpip.c — PTP/IP client for localhost camera control (shutter, bulb, focus, EV) via port 15740
- 2026-10-26: Built star_trails.c — lighten-blend compositor for star trail/light painting, <35MB RAM
- 2026-10-26: Built timelapse_mux.c — MJPEG AVI muxer for JPEG sequences, <2MB RAM
- 2026-10-26: Built a6400_Homebrew_Wizard.py — full Tkinter desktop GUI for setup, SD formatting, asset deployment, film preset config
- 2026-10-26: Created SDK with camera_defs.h (button codes, InfraInputEvent struct, mount points) and 5 example projects with Makefiles and build.bat scripts
- 2026-10-26: Created shell tool suite: probe.sh (L1/L2/L3 access probe), watchdog.sh (runtime supervisor), wifi_offload.sh, intervalometer.sh, focus_bracket.sh, aeb_hdr.sh
- 2026-10-26: Created web darkroom UI (www/index.html + cgi-bin/api.sh) for mobile phone control via camera Wi-Fi
- 2026-10-26: Compiled BusyBox 1.31 static ARMv7 binary (busybox_armv7, 1.1MB) for telnetd/httpd/utilities
- 2026-10-26: Completed full NAND partition dump — 22 raw images in DUMP/ folder covering all partitions
- 2026-10-26: Created comprehensive documentation suite (README, CAPABILITIES, DEVELOPER_GUIDE, DRIVER_SETUP_GUIDE, USER_GUIDE, TROUBLESHOOTING)
- 2026-10-28: Built `astro/shutter.sh` (shot/bulb/half/release), `astro/intervalometer.sh` (count/interval/bulb/delay, --bg, stop/abort/status, clock-scheduled), `astro/probe_shutter.sh`; tested with the camera's own BusyBox 1.13.4 ash under qemu-arm. Research write-up: `research/SHUTTER_TRIGGER_RESEARCH.md`.

## What Worked
- 2026-10-26: pmca-console-v0.18-win.exe serviceshell gives root shell. USB must be Mass Storage mode.
- 2026-10-26: Static ARMv7 ELF with raw syscalls runs on camera from /tmp
- 2026-10-26: /tmp (nflasha10) is persistent vfat, survives reboot - good staging area
- 2026-10-26: LD_PRELOAD via /etc/ld.so.preload is the boot persistence mechanism. Patched libAK8975.so with ARM constructor that double-forks to launch autoexec.sh.
- 2026-10-26: Zig cross-compiler produces working static ARMv7 musl binaries for the camera (used for SDK examples, ritual_daemon, etc.)
- 2026-10-26: BusyBox 1.31 static build provides telnetd (port 2323) and httpd (port 8080) on camera
- 2026-10-26: PTP/IP server runs on camera localhost:15740 when "Ctrl w/ Smartphone" is enabled — allows shutter/focus/EV control from userspace
- 2026-10-26: Camera has ~400-600MB free RAM under normal shooting — plenty for background image processing at nice -n 19
- 2026-10-28: Running the camera's real BusyBox (`nflasha7_unpacked/bin/busybox` + `/lib`) under `qemu-arm -L sysroot` to test scripts. Needs chmod +x on the extracted files and symlinks ld-linux.so.3/libc.so.6/libcrypt.so.1; external applets need wrapper scripts because there is no binfmt in the container.

## What Didn't Work
- 2026-10-26: PlayMemories Camera Apps (PMCA install) - not supported on a6400, Sony removed the platform
- 2026-10-26: OpenMemories-Tweak - requires PMCA app install, cannot be used on a6400 directly
- 2026-10-26: Direct menu modification - /usr is read-only ext2, .uxc files are binary format, no repack tooling exists
- 2026-10-26: /dev/fb0 does NOT exist — display driven by DMP SUGILITE GPU via /dev/dmpgles2 and Guiengine_161H. No direct framebuffer access for overlays.
- 2026-10-28: Treating `/mnt/project-files/Backup.bin` (from /setting) as BK4 format: it is not (starts `ba 3f 00 00 ...`). The parseable BK4 backups are the firmware defaults in `0110_backup/SYSASTRA-DSLR/DX2/CX626*.bin`. The older `analyze_backup_bin.py` 6-byte-entry layout is a guess and not the real format.

## Findings
- 2026-10-26: PMCA `pull` command truncates every file by exactly 32 KB. Chunking with dd compensates.
- 2026-10-26: SD card needs FAT32 formatting (kernel can't mount exFAT). Create dev node with `mknod /tmp/sd b 250 11`.
- 2026-10-26: a6400 uses CXD90045 SoC (4x ARM Cortex-A5, ~600-800 MHz), same as ZV-E10 and ZV-1F
- 2026-10-26: ~1.25 GB physical RAM (mem=1G@0, mem=256M@0x80000000@1), ~400-600MB free under normal shooting
- 2026-10-26: libJiritsu.so present at /usr/lib/ - Jiritsu subsystem exists here
- 2026-10-26: /etc is writable vfat (same partition as /tmp, nflasha10) - used for ld.so.preload
- 2026-10-26: Firmware encryption uses AES-128-ECB + AES-256-CBC with per-model keys. Blocks custom firmware path.
- 2026-10-26: Camera has ~15,000 settings in Backup.bin (/setting/)
- 2026-10-26: Button input passes through snd_InfraInputEvent_to_InputServant in libObj.so (PLT relocatable) — can be intercepted for custom menus
- 2026-10-26: Live view driven by DMP SUGILITE hardware, /dev/dmpgles2, Guiengine_161H — no /dev/fb0. On-screen text can be intercepted via GEN_Label::setText / TextSprite. External HDMI recommended for video overlays.
- 2026-10-26: Sony ARW files contain embedded JPEG preview within first 3MB — can be extracted by scanning for JPEG SOI/EOI markers
- 2026-10-28: Wi-Fi remote shutter = key events. `libInfraRemoteControlNet.so` RcInputShutter/RcInputStartBulbShooting/RcInputStopBulbShooting only call `InfraInputUtil::SendEvtToInput` with logical keys 0x0d (S1) / 0x0e (S2); bulb = hold S2.
- 2026-10-28: Key tables are Backup property 0x7A0001 (`BIN!` blocks + `KEY!` records). Block 0x1a maps remote keys to 0x1A0000xx, so S1=0x1A00000D, S2=0x1A00000E. BLE remote uses source 0x1c.
- 2026-10-28: InfraInput (libObj.so) has a factory test hook on sync OSAL queue 0x007A018C ("Usage: sndcmd.elf --sync 0x007A018C cmd [params]"). cmd 2 = send key event {key_code, sub_key_code, status, ex_info} to 0x007A0020. cmd 1 = input recorder/playback (ILOG).
- 2026-10-28: `sndcmd.elf` args are `size:value` with size b/w/d; `testcmd.elf` takes bare 4-byte numbers.
- 2026-10-28: `/usr/bin/orb-server` is the Sony Camera Remote API (ScalarWebAPI) server on port 10000 (`/sony/camera`), started by libInfraWebApi via WebApiLauncher.sh; has actTakePicture, start/stopBulbShooting, startIntervalStillRec.
- 2026-10-28: Camera BusyBox 1.13.4 ash has no `getopts` and no `printf`; `sleep` takes whole seconds only; `busybox date +%s` and `busybox usleep` work when called through `busybox`.

## Open Issues
- Backup.bin format not fully decoded — need to search for telnet/ADB/debug enable flags
- L3 live view / framebuffer access still research-only (no /dev/fb0, display goes through DMP GPU)
- Wi-Fi offload feature (FTP upload of developed photos) coded but not tested
- autoexec_hook.so boot persistence needs on-camera verification (was ld.so.preload actually tested and confirmed working at boot?)
- None of the new binaries (ritual_daemon, ritual_menu, arw_develop, etc.) have been confirmed running on camera yet — they're cross-compiled but need on-camera testing
- Android partitions (nflasha16/17, 240MB) still unexplored
- 2026-10-28: Unverified on camera: whether InfraInput acts on source-0x1a remote keys when no phone is connected (possible key inhibit). If not, try with Ctrl w/ Smartphone active, or the Remote API on :10000.
- sony_ptpip / intervalometer.sh / star_trails from the local platform are not in the repo; the PTP/IP-on-localhost:15740 claim was never verified.

## Session Log
- 2026-10-26: Thread "Launch homebrew apps from camera menu" - Researched GUI homebrew paths. Created handoff6400_v3.zip.
- 2026-10-26: Thread "Dump a6400 storage for offline research" - Created dump guide, pushed to repo.
- 2026-10-26: Thread "Review latest local homebrew progress" - Reviewed massive local build-out: full homebrew platform with LD_PRELOAD boot persistence, film lab daemon, terminal menu, mobile web UI, desktop wizard, SDK with 5 examples, 6 utility tools, full partition dumps, and comprehensive docs. Updated PROJECT_MEMORY.md.
- 2026-10-28: Thread "Trigger shutter from scripts for astro" - Reverse-engineered shutter path (Wi-Fi remote S1/S2 key events + InfraInput test hook), built astro/ shutter, bulb and intervalometer scripts, tested under qemu with the camera's BusyBox.
