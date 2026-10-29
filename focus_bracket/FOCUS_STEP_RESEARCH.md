# How a script can move focus on the a6400 (firmware v2.00)

Goal: move the focus in small, repeatable steps from a shell script, for
focus stacking. Uses the same InfraInput test hook and `sndcmd.elf` sender
as the shutter trigger (PR #6, `SHUTTER_TRIGGER_RESEARCH.md`).

## Result

Send the key **`FOCUS_DRIVE_NEAR_FAR` (`0x1A000025`)**, press then release,
with a **signed step amount in the event's `ex_info` field**: negative =
near, positive = far, 1 / 3 / 7 = small / medium / large. This is exactly
what the camera's own USB PC remote does for PTP control `0xD2D1`
(Near/Far), see below.

```
# one large step far (+7)
sndcmd.elf --sync 0x007A018C d:2 d:0x1A000025 d:0 d:1 d:7
sndcmd.elf --sync 0x007A018C d:2 d:0x1A000025 d:0 d:0 d:7
# one small step near (-1 as a 32-bit value)
sndcmd.elf --sync 0x007A018C d:2 d:0x1A000025 d:0 d:1 d:0xFFFFFFFF
sndcmd.elf --sync 0x007A018C d:2 d:0x1A000025 d:0 d:0 d:0xFFFFFFFF
```

Focus Mode must be MF. Implemented in `focus.sh`.

### History

- First attempt used `0x1A000029`/`0x1A00002A` from counting key-name
  strings. Wrong: the codes jump from `0x19` to `0x20`; those are AWB lock
  and HFR standby. Fixed before any camera test.
- 2026-10-29 camera test (SEL18135, Focus Mode MF): the S1/S2 shutter keys
  work (10 pictures taken), but `STEP_FOCUS_NEAR`/`FAR` (`0x1A00002F` /
  `0x1A000030`, sent with `ex_info` 0) did nothing. That led to the PC
  remote path below.

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

### 3. The USB PC remote focuses with FOCUS_DRIVE + ex_info

`/usr/lib/libInfraRemote.so`:

- `InfraRemoteUsb::CtrlCode2InputKey(ushort code, signed char value)`
  (`0xb522`) maps PTP control codes to logical remote keys. `0xD2C1` -> `0x0d`
  (S1), `0xD2C2` -> `0x0e` (S2), and **`0xD2D1` (Near/Far) -> `0x25`**
  (`FOCUS_DRIVE_NEAR_FAR`). STEP_FOCUS (`0x2f`/`0x30`) is not used here.
- `InfraRemoteUsb::actionControlDevice` (`0xb890`) reads the control value
  with `ParamStd_Template<short>::GetValue()`, then for key `0x25`
  (`0xbb96`) calls `SendEvtToInput(0x25, 1, 0, value)` and
  `SendEvtToInput(0x25, 0, 0, value)`: press and release back to back, the
  signed value passed through.
- `InfraRemoteBase::SendEvtToInput(key, status, sub, ex_info)` (`0x98d0`)
  maps the logical key through key config `0x1a`, fills
  `{key_code, sub_key_code, status, ex_info}` and posts it to `0x007A0020`,
  the same message the test hook's command 2 builds.

The sign convention (negative = near) follows Sony's published PTP/Remote
SDK convention; `probe_focus.sh` checks it on the camera.

### 4. The camera also has a separate step-focus action

`libObj.so` strings: `ActionExeStepFocus`, `MSGID_SYNC_STEP_FOCUS_CMD`,
`PARAMID_STEP_FOCUS_DIR`, `PARAMID_DATA_STEP_FOCUS_DIRECTION`,
`SetStepFocusForLiro`. So step focus is a discrete camera operation with a
direction, not just a ring emulation. Not traced end to end from key to
action.

## Not used

- `STEP_FOCUS_NEAR` / `STEP_FOCUS_FAR` (`0x1A00002F` / `0x1A000030`): exist
  in the key config but did nothing on the camera with `ex_info` 0. The
  action they lead to (`ModelCamera` action 124 -> `ActionExeStepFocus`,
  `libObj.so` `0x4b5930`) takes a direction parameter from the UI model
  layer; how the key reaches it was not traced.
- Camera Remote API (`orb-server` :10000): `libInfraWebApi.so` only shows
  focus *mode* and touch-AF calls, no focus position call, and it needs
  Ctrl w/ Smartphone plus an HTTP client.

## Open questions (need the camera)

1. ~~Does FOCUS_DRIVE with a non-zero `ex_info` move the lens?~~ Yes:
   confirmed 2026-10-29 on the SEL18135 with steps 7 and 1.
2. Is the sign right (negative = near)?
3. How far does one small step move the SEL18135, so how many per frame?
