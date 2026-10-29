# Script-driven shutter for astro: intervals and bulb > 30 s

These scripts let the a6400 press its own shutter from the service shell (or
from anything the boot hook starts). That gives you:

- **Bulb exposures of any length** (2 min, 10 min, ...) without a remote.
- **An intervalometer** for star trails, time-lapses and stacking, either with
  bulb frames or with the camera's own shutter speed (up to 30 s).

Nothing is installed into firmware, no settings are changed and nothing is
written outside `/tmp`. Sony's own test tool `/usr/bin/sndcmd.elf` does the
work. How this was found is in
[`SHUTTER_TRIGGER_RESEARCH.md`](SHUTTER_TRIGGER_RESEARCH.md).

| File | What it does |
|---|---|
| `shutter.sh` | One shot, one bulb exposure, half-press, or a raw S1/S2 key event |
| `intervalometer.sh` | Repeats shots or bulb frames on a schedule; can run in the background |
| `probe_shutter.sh` | First test: checks the tools, half-presses, then takes one picture |

All three must keep **LF line endings** (`.gitattributes` enforces this on
checkout). They only use commands that exist in the camera's BusyBox 1.13.4.

## How it works, in short

Sony's Wi-Fi remote fires the shutter by sending "virtual button" events to
the camera's input service: S1 (half-press) = key `0x1A00000D`, S2
(full-press) = key `0x1A00000E`. For bulb it presses S1+S2, waits, then lets
go. The input service also has a factory test hook (OSAL queue `0x007A018C`)
whose command 2 injects any key event. `shutter.sh` sends exactly the remote's
key sequence through that hook:

```
sndcmd.elf --sync 0x007A018C d:2 d:<key> d:0 d:<1=press|0=release> d:0
```

## On-camera test (do this first)

1. Camera: power on, **shooting mode** (not playback, not a menu), USB mode
   Mass Storage. Lens cap on is fine. For the first test use **MF** so focus
   can't block the shutter.
2. PC: start the service shell as usual
   (`pmca-console-v0.18-win.exe serviceshell` in `F:\a6400project`).
3. Push the three scripts (from the PMCA prompt, before `shell`):
   ```
   push astro_shutter\shutter.sh /tmp/shutter.sh
   push astro_shutter\intervalometer.sh /tmp/intervalometer.sh
   push astro_shutter\probe_shutter.sh /tmp/probe_shutter.sh
   ```
4. Enter the shell and make them executable:
   ```
   shell
   chmod 755 /tmp/shutter.sh /tmp/intervalometer.sh /tmp/probe_shutter.sh
   ```
5. Half-press test (no picture is taken). Watch the camera screen:
   ```
   /tmp/probe_shutter.sh half
   ```
   Expected: `S1 press sent OK`, and the camera reacts for ~3 s (focus frame,
   AF beep, or the screen wakes up / leaves standby).
6. Full test, takes one picture:
   ```
   /tmp/probe_shutter.sh
   ```
   Expected: shutter sound and a new image.
7. Bulb test: set the mode dial to **M**, turn the shutter speed past 30" until
   it shows **BULB**, then:
   ```
   /tmp/shutter.sh bulb 45
   ```
   Expected: one 45-second exposure (the screen shows the bulb counter).

Please report what happened at steps 5 to 7, including any text printed.

### If nothing happens

- **`sndcmd failed (rc=...)`**: copy the full error. The OSAL message did not
  get through; the hook may need `testcmd.elf` instead, which we can switch to.
- **Every send prints OK but the camera does not react**: the camera is
  probably ignoring the remote's key source while no remote is connected. Turn
  on *Ctrl w/ Smartphone*, connect the phone once (Imaging Edge Mobile), then
  rerun step 5. `probe_shutter.sh` also reports whether the Sony Camera Remote
  API (port 10000) is listening, which is the fallback path.
- **A shot but no bulb**: check the camera really shows BULB; at any other
  speed `bulb N` is just a normal shot.
- **Anything feels stuck** (shutter still open, half-press held):
  `/tmp/shutter.sh release`.

## Using it for star photography

Camera setup: M mode, MF, focus on a bright star, IS off on a tripod,
**Long Exposure NR off** (it adds a dark frame as long as each exposure, and
the camera ignores the shutter during it), Auto Power Off at its longest.

```sh
# Single 4-minute exposure
/tmp/shutter.sh bulb 240

# 30 frames of 2 minutes, one every 125 s (5 s to save each frame), in the background
/tmp/intervalometer.sh -b 120 -n 30 -i 125 --bg

# Star trails with the camera set to 20", one frame every 22 s until stopped
/tmp/intervalometer.sh -n 0 -i 22 --bg

# Wait 10 minutes (e.g. for twilight to end) before the first frame
/tmp/intervalometer.sh -b 60 -n 100 -i 63 -d 600 --bg

/tmp/intervalometer.sh status   # progress + log (/tmp/astro.log)
/tmp/intervalometer.sh stop     # finish the current frame, then stop
/tmp/intervalometer.sh abort    # stop now; also ends a running bulb exposure
```

Options: `-n COUNT` (0 = until stopped), `-i INTERVAL` seconds from the start of
one frame to the next, `-b BULB` seconds, `-d DELAY` seconds, `--bg` to run in
the background and keep going after the service shell closes. Timing is
scheduled against the clock, so it does not drift over long runs. The interval
must be at least bulb + 2 s.

Notes:

- `/tmp` is persistent flash on this camera. The scripts write one short log
  line per frame to `/tmp/astro.log`, which is overwritten on each `--bg` run.
- Battery: a long run needs a full battery or USB power.
- Off-camera dry run (prints the key events instead of sending them):
  `SHUTTER_DRYRUN=1 sh shutter.sh bulb 5`.
