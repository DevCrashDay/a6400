# focus_bracket/ — focus stacking on the a6400

Shoots a series of frames while stepping the focus between shots, for
stacking in Helicon Focus, Zerene Stacker, Photoshop and the like. Runs on
the camera from the service shell; no phone, Wi-Fi or cable.

It injects the same focus Near/Far key the camera's USB PC remote uses
(`0x1A000025` with a signed step size) and the S1/S2 shutter keys through
the camera's InfraInput test hook. See
[FOCUS_STEP_RESEARCH.md](FOCUS_STEP_RESEARCH.md).

**Status:** shutter keys and focus moves confirmed on camera (2026-10-29,
SEL18135). Run `probe_focus.sh` once on a new lens.

## Setup

- Focus mode **MF**, lens focused on the nearest point (or the farthest, for `-d near`)
- M or A mode, Long Exposure NR **off**, tripod
- Copy the four scripts to the camera with LF line endings:

```sh
cp shutter.sh focus.sh probe_focus.sh focus_bracket.sh /tmp/
chmod +x /tmp/probe_focus.sh /tmp/focus_bracket.sh
```

## 1. Probe (takes no pictures)

```sh
/tmp/probe_focus.sh                 # 3 large moves near, then 3 far
/tmp/probe_focus.sh far small 5     # 5 of the smallest moves
```

If anything other than focus changes, stop and report it.

## 2. Bracket

```sh
/tmp/focus_bracket.sh -n 5 -d far                  # small first test
/tmp/focus_bracket.sh -n 30 -s 2 -d far --bg       # real run in background
/tmp/focus_bracket.sh status
/tmp/focus_bracket.sh stop                      # after the current frame
/tmp/focus_bracket.sh abort                     # now, releases all keys
```

| Flag | Default | Meaning |
|---|---|---|
| `-n COUNT` | 10 | Frames to shoot (at least 2) |
| `-s STEPS` | 1 | Focus moves between frames |
| `-z SIZE` | small | Size of each move: `small`, `medium`, `large` or `1`..`7` |
| `-d far\|near` | far | Direction of travel |
| `-w WAIT` | 2 | Seconds after stepping before the shot |
| `-g GAP` | 1 | Seconds between single moves (0 = none) |
| `-i INTERVAL` | 5 | Minimum seconds frame start to frame start |
| `--bg` | | Background run, log in `/tmp/focus.log` |

The script stops on the first failed key send, since the lens position is
then unknown. `SHUTTER_DRYRUN=1` prints the key events instead of sending them.

## Files

| File | |
|---|---|
| `focus_bracket.sh` | The bracket script |
| `probe_focus.sh` | Focus move test, no pictures |
| `focus.sh` | Focus move functions (sourced) |
| `shutter.sh` | Key sender and shutter functions, same as `astro/shutter.sh` from PR #6 |
| `FOCUS_STEP_RESEARCH.md` | How the key codes were found |
