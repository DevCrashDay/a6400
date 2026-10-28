# How a script can step focus on the a6400 (firmware v2.00)

Goal: move the focus plane in discrete steps from a shell script, for focus
stacking brackets. Builds on the shutter trigger research in
[`../astro/SHUTTER_TRIGGER_RESEARCH.md`] — same InfraInput test hook, same
`sndcmd.elf` sender, different virtual keys.

## Result

The camera's network remote key table (Backup property `0x7A0001`, block
`0x1a`) includes focus step keys alongside the shutter keys we already use:

| Key name | Logical | Physical | Purpose |
|---|---|---|---|
| `KEY_REMOTE_S1` | 0x0D | `0x1A00000D` | Half-press (AF / meter) |
| `KEY_REMOTE_S2` | 0x0E | `0x1A00000E` | Full-press (shutter) |
| `KEY_REMOTE_STEP_FOCUS_NEAR` | 0x29 | **`0x1A000029`** | One focus step toward near |
| `KEY_REMOTE_STEP_FOCUS_FAR` | 0x2A | **`0x1A00002A`** | One focus step toward far |
| `KEY_REMOTE_FOCUS_DRIVE_NEAR_FAR` | 0x1F | `0x1A00001F` | Continuous focus drive |

Injecting a STEP_FOCUS key press+release via the test hook should move the
lens one discrete step:

```
# Step focus toward far (one click):
sndcmd.elf --sync 0x007A018C d:2 d:0x1A00002A d:0 d:1 d:0   # press
sndcmd.elf --sync 0x007A018C d:2 d:0x1A00002A d:0 d:0 d:0   # release

# Step focus toward near (one click):
sndcmd.elf --sync 0x007A018C d:2 d:0x1A000029 d:0 d:1 d:0   # press
sndcmd.elf --sync 0x007A018C d:2 d:0x1A000029 d:0 d:0 d:0   # release
```

The camera must be in **MF mode** so that AF does not override the stepped
focus position.

## Evidence chain

### 1. Key table enumeration

The `KEY_REMOTE_*` string table in `libObj.so` lists all network remote
virtual keys in logical-code order. Cross-referencing known keys confirms
the mapping (1-indexed position = logical hex code):

- Position 13 = `KEY_REMOTE_S1` → logical `0x0D` → physical `0x1A00000D` ✓
- Position 14 = `KEY_REMOTE_S2` → logical `0x0E` → physical `0x1A00000E` ✓
- Position 17 = `KEY_REMOTE_ZOOM_TELE` → logical `0x11` → physical `0x1A000011` ✓
- Position 18 = `KEY_REMOTE_ZOOM_WIDE` → logical `0x12` → physical `0x1A000012` ✓
- Position 41 = `KEY_REMOTE_STEP_FOCUS_NEAR` → logical `0x29` → physical `0x1A000029`
- Position 42 = `KEY_REMOTE_STEP_FOCUS_FAR` → logical `0x2A` → physical `0x1A00002A`

### 2. Focus drive messages in the camera app

`libObj.so` contains message handlers that confirm the camera processes
focus drive commands:

- `MSGID_FOCUS_DRIVE_CMD` / `MSGID_SYNC_FOCUS_DRIVE_CMD`
- `MSGID_SYNC_FOCUS_CONT_DRIVE_CMD` (continuous)
- `ActionExeFocusDrive`, `ActionExeFocusContDrive`

These are the internal handlers that STEP_FOCUS key events route to.

### 3. WebAPI also has focus endpoints

`libInfraWebApi.so` exports `getFocusMode`, `setFocusMode`,
`getNearModeInPF`, `setNearModeInPF`, and `setTouchAFPosition`. The Camera
Remote API (`orb-server` on port 10000) is an alternative path for
controlling focus, but requires *Ctrl w/ Smartphone* to be active and an
HTTP client — the key injection path is simpler.

## Full remote key table

For reference, the complete `KEY_REMOTE_*` enumeration (block `0x1a`,
46 entries):

| # | Key name | Logical | Physical |
|---|---|---|---|
| 1 | AEL | 0x01 | 0x1A000001 |
| 2 | AFL | 0x02 | 0x1A000002 |
| 3 | EXP_COMP_MINUS | 0x03 | 0x1A000003 |
| 4 | EXP_COMP_PLUS | 0x04 | 0x1A000004 |
| 5 | FEL | 0x05 | 0x1A000005 |
| 6 | FLASH_COMP_MINUS | 0x06 | 0x1A000006 |
| 7 | FLASH_COMP_PLUS | 0x07 | 0x1A000007 |
| 8 | FNUM_MINUS | 0x08 | 0x1A000008 |
| 9 | FNUM_PLUS | 0x09 | 0x1A000009 |
| 10 | ISO_MINUS | 0x0A | 0x1A00000A |
| 11 | ISO_PLUS | 0x0B | 0x1A00000B |
| 12 | MOV_REC | 0x0C | 0x1A00000C |
| 13 | S1 | 0x0D | 0x1A00000D |
| 14 | S2 | 0x0E | 0x1A00000E |
| 15 | SS_MINUS | 0x0F | 0x1A00000F |
| 16 | SS_PLUS | 0x10 | 0x1A000010 |
| 17 | ZOOM_TELE | 0x11 | 0x1A000011 |
| 18 | ZOOM_WIDE | 0x12 | 0x1A000012 |
| 19 | INTERVAL_REC | 0x13 | 0x1A000013 |
| 20 | PC_MOV_REC | 0x14 | 0x1A000014 |
| 21 | AUDIO_REC | 0x15 | 0x1A000015 |
| 22 | START_SUPER_SLOW_REC_STANDBY | 0x16 | 0x1A000016 |
| 23 | STOP_SUPER_SLOW_REC_STANDBY | 0x17 | 0x1A000017 |
| 24 | CANCEL_SUPER_SLOW_REC_RECORDING | 0x18 | 0x1A000018 |
| 25 | AF_AREA_POINT | 0x19 | 0x1A000019 |
| 26 | FOCUS_MAGNIFIER | 0x1A | 0x1A00001A |
| 27 | UP | 0x1B | 0x1A00001B |
| 28 | DOWN | 0x1C | 0x1A00001C |
| 29 | LEFT | 0x1D | 0x1A00001D |
| 30 | RIGHT | 0x1E | 0x1A00001E |
| 31 | FOCUS_DRIVE_NEAR_FAR | 0x1F | 0x1A00001F |
| 32 | AF_MF_CHANGE | 0x20 | 0x1A000020 |
| 33 | AF_MF_HOLD | 0x21 | 0x1A000021 |
| 34 | PIXEL_SHIFT_SHOOTING_CANCEL | 0x22 | 0x1A000022 |
| 35 | AWBL | 0x23 | 0x1A000023 |
| 36 | HFR_STANDBY | 0x24 | 0x1A000024 |
| 37 | HFR_RECORDING_CANCEL | 0x25 | 0x1A000025 |
| 38 | TRANSFER_MTP_MODE | 0x26 | 0x1A000026 |
| 39 | TRANSFER_CAPTURE_MODE | 0x27 | 0x1A000027 |
| 40 | FORMAT | 0x28 | 0x1A000028 |
| 41 | STEP_FOCUS_NEAR | 0x29 | 0x1A000029 |
| 42 | STEP_FOCUS_FAR | 0x2A | 0x1A00002A |
| 43 | DATE_TIME_SETTLED | 0x2B | 0x1A00002B |
| 44 | WHITE_BALANCE_INITIALIZE | 0x2C | 0x1A00002C |
| 45 | PROGRAM_SHIFT_PLUS | 0x2D | 0x1A00002D |
| 46 | PROGRAM_SHIFT_MINUS | 0x2E | 0x1A00002E |

## Open questions (need the camera)

1. Does STEP_FOCUS_NEAR/FAR move the lens one step in MF mode? `probe_focus.sh`
   answers this without taking a picture.
2. Are the step keys inhibited when no remote is connected (same open question
   as S1/S2)?
3. How large is one step? It likely depends on the lens and the focus ring's
   virtual travel. The `-s` flag in `focus_bracket.sh` lets the user dial in
   the right granularity.
4. Does FOCUS_DRIVE_NEAR_FAR need a sub_key_code for direction, or does it
   use press = near / release = far (like zoom)?
