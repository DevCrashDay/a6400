# focus_bracket/ — Focus Stacking for the a6400

Shoot a series of frames while stepping the focus between each shot,
for later stacking in Helicon Focus, Zerene Stacker, Photoshop, or
similar software. Runs entirely on the camera via the service shell —
no phone, Wi-Fi, or cable needed.

## How it works

The camera's InfraInput test hook accepts virtual key events. We inject
`KEY_REMOTE_STEP_FOCUS_NEAR` (`0x1A000029`) or `KEY_REMOTE_STEP_FOCUS_FAR`
(`0x1A00002A`) to move the focus plane, then fire the shutter with the same
S1/S2 keys used by `astro/shutter.sh`.

See [`FOCUS_STEP_RESEARCH.md`](FOCUS_STEP_RESEARCH.md) for the full evidence
chain and the complete remote key table.

## Prerequisites

- Camera in **MF** mode (so AF does not override the focus steps)
- Lens focused manually on the nearest (or farthest) subject
- Shutter speed, aperture, ISO set as desired (M or A mode recommended)
- Long Exposure NR **off** (avoids double-length waits between frames)
- Tripod or stable surface

## Quick start

### 1. Test focus stepping (no pictures taken)

```sh
# Copy scripts to camera
cp focus_bracket.sh probe_focus.sh shutter.sh /tmp/
chmod +x /tmp/probe_focus.sh /tmp/focus_bracket.sh /tmp/shutter.sh

# Run the probe — watch live view for focus movement
/tmp/probe_focus.sh
```

### 2. Shoot a focus bracket

```sh
# 20 frames, 2 steps between each, near-to-far
/tmp/focus_bracket.sh -n 20 -s 2 -d far

# 30 fine slices (1 step each), run in background
/tmp/focus_bracket.sh -n 30 -s 1 -d far --bg

# Check progress
/tmp/focus_bracket.sh status

# Stop after the current frame
/tmp/focus_bracket.sh stop
```

## Options

| Flag | Default | Description |
|---|---|---|
| `-n COUNT` | 10 | Number of frames to shoot |
| `-s STEPS` | 2 | Focus steps between each frame |
| `-d DIR` | far | Direction: `far` (near-to-far) or `near` (far-to-near) |
| `-w WAIT` | 2 | Seconds after stepping before taking the shot |
| `-i INTERVAL` | 5 | Minimum seconds between frames (start-to-start) |
| `--settle MS` | 200 | Pause between individual focus key events (ms) |
| `--bg` | — | Run in background, log to `/tmp/focus.log` |

## Tips

- **Step size** depends on the lens. Start with `-s 1` and check whether
  adjacent frames overlap in depth of field. Increase if there is too much
  overlap (wastes frames) or decrease if there are gaps.
- **Direction**: for macro work, start focused at the nearest point and
  step `far`. For landscapes, start at the far point and step `near`.
- **Dry run**: set `SHUTTER_DRYRUN=1` to print the key events without
  sending them: `SHUTTER_DRYRUN=1 /tmp/focus_bracket.sh -n 5 -s 2`

## Files

| File | Description |
|---|---|
| `focus_bracket.sh` | Main focus stacking script |
| `probe_focus.sh` | Test focus stepping without taking pictures |
| `shutter.sh` | Shutter trigger library (same as `astro/shutter.sh`) |
| `FOCUS_STEP_RESEARCH.md` | Firmware analysis: how focus stepping works |

## Status

**Untested on camera.** The key codes are derived from static firmware
analysis (same method that found the shutter keys in PR #6). Run
`probe_focus.sh` first to confirm focus stepping works before shooting
a real bracket.
