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

## What the result decides

- **No `/dev/mem` and no large shared region:** the camera image can't be
  reached from Linux. Open gate is a dead end and this folder stops here.
- **Camera-core memory is visible:** the next step is still read-only:
  finding the video format table (`3840X2160_24P` and its neighbours) in
  live memory. Changing it would come much later, and only after the sensor
  timing for a 3:2 mode is understood. A wrong sensor setting can hang the
  camera until the battery is pulled.
