# How a script can fire the a6400 shutter (firmware v2.00)

Goal: trigger the shutter from on-camera code, for an intervalometer and for
bulb exposures longer than 30 s. All addresses below are from the v2.00
firmware extracted into `fw_v200_extracted/` (`/usr` = `nflasha15`). Findings
come from static analysis (ARM/Thumb disassembly with capstone, string and
symbol tables). On-camera confirmation is still pending; see
[`astro/README.md`](../astro/README.md) for the test steps.

## Result

The camera's input service accepts injected key events through a factory test
hook, and Sony's own Wi-Fi remote fires the shutter purely with key events.
Combining the two gives a script-only shutter:

```
/usr/bin/sndcmd.elf --sync 0x007A018C d:2 d:0x1A00000D d:0 d:1 d:0   # S1 press
/usr/bin/sndcmd.elf --sync 0x007A018C d:2 d:0x1A00000E d:0 d:1 d:0   # S2 press
/usr/bin/sndcmd.elf --sync 0x007A018C d:2 d:0x1A00000E d:0 d:0 d:0   # S2 release
/usr/bin/sndcmd.elf --sync 0x007A018C d:2 d:0x1A00000D d:0 d:0 d:0   # S1 release
```

Bulb = press S1, press S2, wait N seconds, release S2, release S1 (with the
camera set to BULB). Implemented in `astro/shutter.sh` and
`astro/intervalometer.sh`.

## Evidence chain

### 1. The Wi-Fi remote presses virtual S1/S2 keys

`/usr/lib/libInfraRemoteControlNet.so` (the Camera Remote API backend inside
the camera app) exports small functions that only call
`NetRemoteLibrary_::InfraInputUtil::SendEvtToInput(this, key, state)`:

| Function | Address | Calls |
|---|---|---|
| `RcInputS1On()` | `0x18608` | (0x0d, 1) |
| `RcInputS1Off()` | `0x18624` | (0x0d, 0) |
| `RcInputS2Off()` | `0x18640` | (0x0e, 0) |
| `RcInputShutter()` | `0x185d0` | (0x0d,1) (0x0e,1) (0x0e,0) (0x0d,0) |
| `RcInputStartBulbShooting()` | `0x186c8` | (0x0d,1) (0x0e,1) |
| `RcInputStopBulbShooting()` | `0x18710` | (0x0e,0) (0x0d,0) |
| `RcInputZoomTeleOn()` etc. | `0x18494` | (0x11, 1) ... zoom keys 0x11/0x12 |

So logical key 0x0d is S1 (half-press) and 0x0e is S2 (full-press), and
remote bulb is literally "hold S2". `RcInputInitialize()` (`0x18234`) builds
the `InfraInputUtil` with key-config id `0x1a`.

### 2. How SendEvtToInput delivers the key

`/usr/lib/libNetRemote.so`, `InfraInputUtil::SendEvtToInput` at `0x2a44`:

1. `GetPhysKeyCode(logical)` maps the logical key through key-config table `0x1a`.
2. `osal_valloc_msg_wait(0x007A0020, &msg, 16, 1)`.
3. Fills a 16-byte `InfraInputPhyKeyData {key_code, sub_key_code=0, status, ex_info=0}`.
4. `osal_snd_msg(0x007A0020, msg)`.

`0x007A0020` is the InfraInput service queue (OSAL subsystem `0x7a`).

### 3. The physical key codes live in Backup.bin

`PhyKeyConfigAlloc(id)` in `libObj.so` (`0x863470`, helper `0x8633bc`)
reads Backup property **`0x007A0001`** (`Backup_get_datasize` / `Backup_read`).
That property is a list of `BIN!` blocks. Parsed from the factory default
backup `0110_backup/.../CX62600_UC2.bin`:

- Block id `0x1a` (network remote): logical `0x01..0x34` -> `0x1A000001..0x1A000034`.
  So **S1 = `0x1A00000D`, S2 = `0x1A00000E`**.
- Block id `0x02` is the `KEY!` table (617 records). It has records for
  `0x1A00000D` (local code `0x0d`) and `0x1A00000E` (local code `0x0e`) under
  key source `0x1a`, so InfraInput knows both keys.
- Other virtual key sources in the same table: `0x1c` (Bluetooth LE remote,
  used by `libInfraBluetoothLowEnergy.so` with source id `0x1a00581`), `0x07`
  (USB control), `0x06`, `0x0a`, `0x0c`, `0x10`, `0x12`.

### 4. InfraInput has a test hook that injects key events

`libObj.so` contains `testCmdCallback(void*, void*)` (`0x85efa8`), registered
at `0x85f710` with `osal_reg_sync_msg_queue_cb(0x007A018C, ...)`. Its help
string is literally:

```
[INFRAINPUT] Usage: sndcmd.elf --sync 0x007A018C cmd [params]
```

Commands found in the dispatcher:

| cmd | Handler | Meaning |
|---|---|---|
| 1 | `0x85f04a` | ILOG input recorder: 1 rec, 2 play, 3 stop, 4 save, 5 reboot, 6 getpos |
| **2** | **`0x85f174`** | **SEND_EVENT: params = key_code, sub_key_code, status, ex_info** |
| 3 | `0x85f1e8` | Dump status table for a key (to the debug log) |
| 4..6, 8, 10..13 | | Set/get key inhibit, event inhibit, key repeat, event status |
| 0x1000 / 0x2000 | | Debug print switches |

Command 2 copies the four params into an `InfraInputPhyKeyData` and sends it
to `0x007A0020` with source id `0x007A018C` (helper `0x85eea8`): the same
message `SendEvtToInput` sends.

### 5. sndcmd.elf argument format

`/usr/bin/sndcmd.elf` uses `/usr/lib/libtestcmd.so`. Each data argument is
`size:value` where size is `b` (1 byte), `w` (2) or `d` (4), parsed by the
tokenizer at `0x16d8` (`strtoull`, base auto-detected). Payload words are
packed back to back, so `d:2 d:KEY d:0 d:STATE d:0` = `{cmd, key_code,
sub_key_code, status, ex_info}`. `--sync` uses `osal_snd_sync_msg`, which the
hook's sync queue needs. (`/usr/bin/testcmd.elf` takes bare 4-byte numbers
and is an alternative sender.)

## Other paths found (fallbacks)

- **Sony Camera Remote API**: `/usr/bin/orb-server` is a full ScalarWebAPI
  server. `libInfraWebApi.so` starts it via `/usr/bin/WebApiLauncher.sh`
  (`orb-server -f /usr/share/network/remotecontrol -p 10000`), so the endpoint
  is `http://<camera>:10000/sony/camera` (it also has an internal
  `/sony/localloopback` path and references `127.0.0.1`). It lists
  `actTakePicture`, `startBulbShooting`, `stopBulbShooting`,
  `setShutterSpeed`, `startIntervalStillRec`, and more. It is most likely only
  started while *Ctrl w/ Smartphone* is active, and its shutter calls most
  likely end in the same `RcInput*` functions (inferred, not traced end to end). Needs an HTTP client on the camera (the stock
  BusyBox has no `wget`; the homebrew `busybox` 1.31 does).
- **USB PC Remote** (`libInfraRemote.so`, `MtpRemotePcOperation::SDIOControlDevice`)
  is the Sony PTP S1/S2 path for a tethered PC, not reachable from on-camera code.
- **PTP/IP on 15740**: earlier notes claimed it listens on localhost with
  *Ctrl w/ Smartphone* on. Not verified, and the local `sony_ptpip` tool is
  not in this repo. `probe_shutter.sh` reports whether 10000/15740 listen.

## Open questions (need the camera)

1. Does the camera act on source-`0x1a` keys when no remote is connected, or
   does it inhibit them (InfraInput has per-key inhibit flags)? The probe's
   half-press step answers this without taking a picture.
2. Does `sndcmd.elf --sync` return 0 on success and print anything?
3. Does AF-S focus priority block S2 when focus fails? (Use MF for astro.)

## Tools used

Small Python scripts (capstone + pyelftools) to disassemble and resolve PLT
names, a BK4 Backup.bin parser for property `0x7A0001`, and the
camera's own BusyBox 1.13.4 (from `nflasha7`) under `qemu-arm` to test the
scripts with the real shell.
