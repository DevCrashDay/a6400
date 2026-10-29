# Remove 30-Minute Recording Limit — Sony a6400

## What this does

Removes the ~30-minute continuous video recording limit on the Sony a6400
(ILCE-6400, firmware v2.00). After applying, the camera can record a single
clip up to approximately **13 hours** (limited only by card space, battery,
and thermal shutdown).

## How it works

Sony stores the maximum recording time in three **Backup.bin** properties:

| Property ID  | Meaning | Factory value | Unrestricted value |
|-------------|---------|---------------|-------------------|
| `0x003c0373` | Hours   | `0x00` (0)    | `0x0D` (13)       |
| `0x003c0374` | Minutes | `0x1D` (29)   | `0x01` (1)        |
| `0x003c0375` | Seconds | `0x32` (50)   | `0x00` (0)        |

**Factory:** 0 hours, 29 minutes, 50 seconds (just under the EU import
duty threshold for "video cameras").

**Unrestricted:** 13 hours, 1 minute, 0 seconds. This is the value Sony
uses on their own camcorders that ship without the limit. The internal
timer uses a 90 KHz clock; the theoretical overflow is at 13h 15m 21s,
so 13h 01m 00s gives a safe margin.

These are the same property IDs used by
[OpenMemories-Tweak](https://github.com/ma1co/OpenMemories-Tweak) (via
[OpenMemories-Platform](https://github.com/ma1co/OpenMemories-Platform)),
which has been used safely on thousands of Sony cameras since 2016.

The setting is read by the firmware at recording start from the in-memory
backup table. The `bk` tool (built into the camera at `/usr/bin/bk.elf`)
reads and writes these properties. Changes take effect on the next
recording without needing a reboot.

## Usage

### Manual (via PMCA service shell)

```
# Check current limit
/usr/bin/bk.elf r 003c0373   # Hours   → expect 00
/usr/bin/bk.elf r 003c0374   # Minutes → expect 1D (=29)
/usr/bin/bk.elf r 003c0375   # Seconds → expect 32 (=50)

# Remove the limit
/usr/bin/bk.elf w 003c0373 0D
/usr/bin/bk.elf w 003c0374 01
/usr/bin/bk.elf w 003c0375 00
/usr/bin/bk.elf s

# Restore factory limit
/usr/bin/bk.elf w 003c0373 00
/usr/bin/bk.elf w 003c0374 1D
/usr/bin/bk.elf w 003c0375 32
/usr/bin/bk.elf s
```

### Automated (via rec_unlimit.sh)

Copy `rec_unlimit.sh` to the SD card at `/mnt/a6400hb/` or `/mnt/`:

```
# Remove the limit
sh /mnt/a6400hb/rec_unlimit.sh

# Check status
sh /mnt/a6400hb/rec_unlimit.sh --status

# Restore factory limit
sh /mnt/a6400hb/rec_unlimit.sh --restore
```

### Boot-time (via autoexec.sh)

Place the flag file `enable_rec_unlimit` on the SD card root:

```
touch /mnt/enable_rec_unlimit
```

When `autoexec.sh` runs at boot, it checks for this flag and applies the
unlimit automatically. Remove the flag to stop auto-applying.

To force-restore the factory limit at boot, create `disable_rec_unlimit`:

```
touch /mnt/disable_rec_unlimit
```

## How to undo

Three ways, any one works:

1. **From service shell:** Run `sh rec_unlimit.sh --restore`
2. **From service shell (manual):** Write factory values with `bk.elf w` as shown above
3. **At boot:** Create `/mnt/disable_rec_unlimit` on the SD card — autoexec will restore factory values

The change only modifies three bytes in the settings database. A factory
reset from the camera menu will also restore the original values.

## Safety

- **Reversible:** The original values are well-known and can always be
  written back.
- **No firmware modification:** Only the settings database (Backup.bin)
  is changed, which is designed to be user-writable.
- **Battle-tested values:** The 13h 01m 00s value is used by Sony's own
  camcorder firmware and by OpenMemories-Tweak across thousands of cameras.
- **No bricking risk:** Even wrong values in these three properties cannot
  brick the camera. The worst case is a recording that stops at an
  unexpected time.

## Technical details

The firmware references `S_SPLITMOVIE_ONOFF` in `libObj.so`,
`libsencore.so`, `libJiritsu.so`, and `libInfraMediaCommon.so`. The
recording engine reads the H/M/S limit from Backup.bin, converts it to
90 KHz ticks, and stops the recording when the timer reaches that value.
The value is checked at recording start, not continuously — so changing it
while a recording is in progress has no effect until the next recording.
