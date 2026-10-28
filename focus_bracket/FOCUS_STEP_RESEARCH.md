# How a script can step focus on the a6400 (firmware v2.00)

Goal: move the focus in small, repeatable steps from a shell script, for
focus stacking. This uses the same InfraInput test hook and `sndcmd.elf`
sender as the shutter trigger (PR #6, `SHUTTER_TRIGGER_RESEARCH.md`), with
different virtual keys. Static analysis only; on-camera confirmation is
still pending (run `probe_focus.sh`).

## Result

The Wi-Fi remote's key set (key source `0x1a`) has two step-focus keys:

| Key | Code |
|---|---|
| `KEY_REMOTE_STEP_FOCUS_NEAR` | **`0x1A00002F`** |
| `KEY_REMOTE_STEP_FOCUS_FAR` | **`0x1A000030`** |

One step = press and release:

```
sndcmd.elf --sync 0x007A018C d:2 d:0x1A000030 d:0 d:1 d:0   # FAR press
sndcmd.elf --sync 0x007A018C d:2 d:0x1A000030 d:0 d:0 d:0   # FAR release
sndcmd.elf --sync 0x007A018C d:2 d:0x1A00002F d:0 d:1 d:0   # NEAR press
sndcmd.elf --sync 0x007A018C d:2 d:0x1A00002F d:0 d:0 d:0   # NEAR release
```

Focus mode must be MF, otherwise AF moves the lens back.

**Correction (2026-10-28):** the first version of this PR used `0x1A000029`
and `0x1A00002A`, found by counting the order of the key-name strings. That
was wrong: the codes skip `0x1A..0x1F`. `0x1A000029` is AWB lock and
`0x1A00002A` is HFR standby, which would have switched the camera into
high frame rate mode. The codes above come from the firmware's own
code-to-name table.

## Evidence

### 1. Code-to-name table in libObj.so

`/usr/lib/libObj.so` has a debug table of `{uint32 key_code, char *name}`
pairs (file offset around `0x13e2a50`, pointers into `.rodata` at
`0x1010ed2`..). The entries for source `0x1a`:

| Code | Name | Code | Name |
|---|---|---|---|
| `0x1A000001` | AEL | `0x1A000020` | FOCUS_MAGNIFIER |
| `0x1A000002` | AFL | `0x1A000021` | UP |
| `0x1A000003` | EXP_COMP_MINUS | `0x1A000022` | DOWN |
| `0x1A000004` | EXP_COMP_PLUS | `0x1A000023` | LEFT |
| `0x1A000005` | FEL | `0x1A000024` | RIGHT |
| `0x1A000006` | FLASH_COMP_MINUS | `0x1A000025` | FOCUS_DRIVE_NEAR_FAR |
| `0x1A000007` | FLASH_COMP_PLUS | `0x1A000026` | AF_MF_CHANGE |
| `0x1A000008` | FNUM_MINUS | `0x1A000027` | AF_MF_HOLD |
| `0x1A000009` | FNUM_PLUS | `0x1A000028` | PIXEL_SHIFT_SHOOTING_CANCEL |
| `0x1A00000A` | ISO_MINUS | `0x1A000029` | AWBL |
| `0x1A00000B` | ISO_PLUS | `0x1A00002A` | HFR_STANDBY |
| `0x1A00000C` | MOV_REC | `0x1A00002B` | HFR_RECORDING_CANCEL |
| `0x1A00000D` | S1 | `0x1A00002C` | TRANSFER_MTP_MODE |
| `0x1A00000E` | S2 | `0x1A00002D` | TRANSFER_CAPTURE_MODE |
| `0x1A00000F` | SS_MINUS | `0x1A00002E` | FORMAT |
| `0x1A000010` | SS_PLUS | **`0x1A00002F`** | **STEP_FOCUS_NEAR** |
| `0x1A000011` | ZOOM_TELE | **`0x1A000030`** | **STEP_FOCUS_FAR** |
| `0x1A000012` | ZOOM_WIDE | `0x1A000031` | DATE_TIME_SETTLED |
| `0x1A000013` | INTERVAL_REC | `0x1A000032` | WHITE_BALANCE_INITIALIZE |
| `0x1A000014` | PC_MOV_REC | `0x1A000033` | PROGRAM_SHIFT_PLUS |
| `0x1A000015` | AUDIO_REC | `0x1A000034` | PROGRAM_SHIFT_MINUS |
| `0x1A000016` | START_SUPER_SLOW_REC_STANDBY | | |
| `0x1A000017` | STOP_SUPER_SLOW_REC_STANDBY | | |
| `0x1A000018` | CANCEL_SUPER_SLOW_REC_RECORDING | | |
| `0x1A000019` | AF_AREA_POINT | | |

S1 = `0x1A00000D` and S2 = `0x1A00000E` match the shutter research, which
found them independently through `RcInputS1On()`.

Do not send keys you have not checked against this table. `0x1A00002E`
(FORMAT) is next to the focus keys.

### 2. The keys exist in the camera's key configuration

In the factory default backup `0110_backup/SYSASTRA-DSLR/DX2/CX62600_UC2.bin`
(Backup property `0x7A0001`):

- The source `0x1a` `BIN!` block maps logical `0x07..0x19` and `0x20..0x34`
  one-to-one to `0x1A0000xx`, including `0x2f` and `0x30` (codes at file
  offsets `0x846d4` and `0x846dc`). `0x1a..0x1f` are absent, which matches
  the gap in the name table.
- The `KEY!` table has records for both, in the same form as S1/S2
  (offsets `0x83620` and `0x83650`): `4b455921 1a000000 2f00001a 2f000000 ...`
  and the same with `30` for FAR.

### 3. The camera has a step-focus action

`libObj.so` strings: `ActionExeStepFocus`, `MSGID_SYNC_STEP_FOCUS_CMD`,
`PARAMID_STEP_FOCUS_DIR`, `PARAMID_DATA_STEP_FOCUS_DIRECTION`,
`SetStepFocusForLiro`. So step focus is a discrete camera operation with a
direction, not just a ring emulation. Not traced end to end from key to
action.

## Not used

- `FOCUS_DRIVE_NEAR_FAR` (`0x1A000025`): continuous drive, its direction and
  speed probably travel in the key's extra data. Unclear format, so the
  step keys are used instead.
- Camera Remote API (`orb-server` :10000): `libInfraWebApi.so` only shows
  focus *mode* and touch-AF calls, no focus position call, and it needs
  Ctrl w/ Smartphone plus an HTTP client.

## Open questions (need the camera)

1. Does a STEP_FOCUS press move the lens in MF? (`probe_focus.sh`)
2. Are source-`0x1a` keys ignored with no remote connected? (Same question
   as S1/S2.)
3. How big is one step? Probably lens dependent, so `-s` sets steps per frame.
4. Does a held key repeat? The scripts press and release at once to avoid it.
