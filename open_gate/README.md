# open_gate/ — open-gate (3:2) video research

**Status:** research only. The assessment (2026-10-29) found that open-gate
video can't be done with settings: the sensor modes and video formats are
compiled into the camera image `av-cam.bin`, which has only 16:9 and SD
formats, and Backup.bin has no aspect or sensor-area property. Any attempt
would mean changing that image while it runs. The first question is whether
Linux can see its memory at all.

## Step 1: read-only memory probe

`probe_rtos_mem.sh` lists what the kernel exposes: the physical address map
(`/proc/iomem`), whether `/dev/mem` exists, loaded modules, and which device
nodes and shared regions the camera apps map. It never reads or writes raw
memory, changes no settings, and writes only `/tmp/open_gate_probe.txt`.

1. Camera on, shooting mode, USB mode Mass Storage.
2. PC: `pmca-console-v0.18-win.exe serviceshell`
3. From the PMCA prompt:
   ```
   push open_gate\probe_rtos_mem.sh /tmp/probe_rtos_mem.sh
   shell
   chmod +x /tmp/probe_rtos_mem.sh
   /tmp/probe_rtos_mem.sh
   exit
   pull /tmp/open_gate_probe.txt open_gate_probe.txt
   ```
   (`pull` cuts about 32 KB off the end of files. This report is usually
   smaller than that, but copy the shell output too if it looks short.)
4. Share `open_gate_probe.txt`.

## Probe result (2026-10-29, Ash's camera, fw 2.00)

- `/dev/mem` exists (root, group 9) and the main camera process (pid 152)
  maps it itself at many physical addresses (0x10xxxxxx, 0x20xxxxxx, and
  0xF... registers). So camera-core memory is visible from Linux.
- Linux only manages 112 MB. Most RAM is reserved for the camera core
  (`memrsv` in `/proc/cmdline`: 0x09915000+167 MB, 0x14000000+704 MB,
  0x80E00000+242 MB). Image buffers go through `/dev/stream` and `/dev/stream2`.
- The warm-boot image (`wbimem=`) covers 0x10000000 (9.4 MB) and
  0x20200000 (57 MB), likely where the camera image's code and data live.

## Step 2: find the format table in memory

**v1 crashed the camera (2026-10-29).** It read all RAM from physical 0,
and reading 0x00004000 (inside `memrsv=1M@0`) caused an external abort
(`read_mem` -> `__copy_to_user_std`). The kernel panicked and the camera
rebooted on its own. Nothing was written. Not every RAM address is readable
through `/dev/mem`, even when `/proc/iomem` lists it as System RAM.

`scan_format_table.sh` v2 reads only the warm-boot regions (`wbimem=` in
`/proc/cmdline`: 0x20028000, 0x10000000, 0x20200000). The kernel reads
these itself when it saves the warm-boot image, and the main app maps parts of
them. It lists each 256 KB block that contains `3840X2160_24P`. It is
read-only. If it still crashes, the camera reboots itself as before.

```
push open_gate\scan_format_table.sh /tmp/scan_format_table.sh
shell
chmod +x /tmp/scan_format_table.sh
/tmp/scan_format_table.sh
exit
pull /tmp/open_gate_scan.txt open_gate_scan.txt
```

Note that finding the table only tells us where the format list lives.
The sensor drive modes for each format are separate, and a 3:2 mode would
still have to be built from scratch.

**v2 result:** no hits in the warm-boot regions, and no crash. The
warm-boot image is most likely Linux's own snapshot, not the camera core.

## Step 3: check reads, then try the camera core's own block

`scan_core_image.sh` first reads one known-good page to prove `/dev/mem`
reads return data. Then it reads the first page of the 14 MB reserved block
at 0x80000000 (`memrsv=14M@0x80000000`), which is about the size of
`av-cam.bin`. If that page is protected, the camera reboots, as with v1.
Otherwise it scans the 14 MB. Output goes to `/tmp/open_gate_core.txt`.

## Alternative: dump RAM to the SD card (analyze on the PC)

`dump_ram_to_sd.sh` copies the same safe regions to `RAMDUMP/` on the SD
card (about 67 MB), so the searching happens on the PC instead of with slow
on-camera grep and lossy `pull`. Add `--core` to also dump the 14 MB at
0x80000000, which may reboot the camera (the other dumps are saved first).
The card must be FAT32.

```
push open_gate\dump_ram_to_sd.sh /tmp/dump_ram_to_sd.sh
shell
chmod +x /tmp/dump_ram_to_sd.sh
/tmp/dump_ram_to_sd.sh --core
```

## What the step 1 result decided

- **No `/dev/mem` and no large shared region:** the camera image can't be
  reached from Linux. Open gate is a dead end and this folder stops here.
- **Camera-core memory is visible:** the next step is still read-only:
  finding the video format table (`3840X2160_24P` and its neighbours) in
  live memory. Changing it would come much later, and only after the sensor
  timing for a 3:2 mode is understood. A wrong sensor setting can hang the
  camera until the battery is pulled.
