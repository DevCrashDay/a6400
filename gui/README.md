# On-Camera Freestanding GUI (`gui/`)

Minimal freestanding C runtime (`rt.h`, `rt.c`), Linux framebuffer graphics library (`fb.h`, `fb.c`), 11x22 bitmap font (`font.h`, `tools/mkfont.py`), and on-camera OSD menu (`main.c` -> `a6400_gui.elf`) for the Sony a6400 (Linux 3.0.27, ARMv7 Cortex-A5).

Because `a6400_gui.elf` uses raw ARM EABI syscalls directly (`_start` in `rt.c`) and includes software integer division helpers (`__aeabi_uidiv`, `__aeabi_idiv`) for the Cortex-A5, it has **zero `libc` dependencies** (~25 KB static binary) and runs regardless of the camera's glibc version.

---

## Folder Contents

| File | Description |
|---|---|
| `src/rt.h`, `src/rt.c` | Freestanding ARMv7 runtime (`_start`, raw syscalls, AEABI division/mem helpers, `fmt`/`fdprintf`, `xalloc`/`xfree`, file I/O, `rt_sigreturn_tramp` signals) |
| `src/fb.h`, `src/fb.c` | Framebuffer abstraction (`mmap` with automatic `lseek+rw` fallback), non-destructive region claim & restore (`fb_claim`, `fb_restore`), pixel packing (`fb_rgb`), and drawing primitives (`gfx_fill`, `gfx_frame`, `gfx_text`) |
| `src/font.h` | 1-bit monospace bitmap font (`DejaVuSansMono-Bold` @ 18px, 11x22 glyphs, ASCII 32..126) |
| `src/main.c` | On-camera OSD menu & hardware probe (`info`, `banner`, interactive `menu`, and background `--daemon` modes) |
| `tools/mkfont.py` | Python/Pillow generator script that converts any TTF font into `src/font.h` |
| `probe_gui.sh` | On-camera diagnostic script for `/dev/fb*`, `/dev/dmp*`, `/proc/fb`, and `/proc/input` |
| `a6400_gui.elf` | Prebuilt static ARMv7 binary ready to copy to `/tmp` or `/mnt` |
| `build.bat`, `Makefile` | Build scripts using `zig cc` |

---

## Copying to the Camera

### Option A: Via SD Card (Recommended)

1. Copy `gui/a6400_gui.elf` and `gui/probe_gui.sh` to the root of your FAT32 SD card.
2. If you have the boot hook (`autoexec.sh`) installed, `a6400_gui.elf` is automatically copied to `/tmp/a6400_gui.elf` on boot.
3. Otherwise, in the `pmca-console serviceshell`:
   ```sh
   mount -t vfat /dev/mmca1 /mnt 2>/dev/null
   cp /mnt/a6400_gui.elf /tmp/a6400_gui.elf
   cp /mnt/probe_gui.sh /tmp/probe_gui.sh
   chmod 755 /tmp/a6400_gui.elf /tmp/probe_gui.sh
   ```

### Option B: Push Over USB (`pmca-console serviceshell`)

Before typing `shell` at the PMCA prompt:
```
push gui\a6400_gui.elf /tmp/a6400_gui.elf
push gui\probe_gui.sh /tmp/probe_gui.sh
shell
```
Then inside the camera shell:
```sh
chmod 755 /tmp/a6400_gui.elf /tmp/probe_gui.sh
```

---

## Usage on Camera

### 1. Probe Display (`/dev/fb0`) & Button Input (`/proc/input`)

```sh
/bin/sh /tmp/probe_gui.sh
```
Or directly via the binary:
```sh
/tmp/a6400_gui.elf info
```

### 2. Draw a Non-Destructive Test Banner (3 Seconds)

```sh
/tmp/a6400_gui.elf banner "Hello from a6400 Homebrew!" 3
```
Claims a centered rectangle on `/dev/fb0`, saves the underlying pixels, draws a framed OSD banner, waits 3 seconds, and restores the original screen contents.

### 3. Launch Interactive OSD Menu

```sh
/tmp/a6400_gui.elf
```
Or run in background daemon mode (hidden until `C1` is pressed or `show` is sent to `/tmp/gui.cmd`):
```sh
/tmp/a6400_gui.elf --daemon &
```

#### Camera Button Controls (`kmod/input.ko` via `/proc/input`)

| Camera Button | Physical Code | Action in OSD Menu |
|---|---|---|
| **Up / DISP** | `0x0003` / `0x01f9` | Previous menu row |
| **Down** | `0x0030` | Next menu row |
| **Left / Wheel CCW** | `0x0015` / `0x10000000` | Previous option / value |
| **Right / Wheel CW** | `0x01af` / `0x10000001` | Next option / value |
| **Center (Enter)** | `0x01ac` | Execute selected item / Save setting |
| **MENU / Trash (C2)** | `0x01ab` / `0x01f8` | Hide OSD and restore screen |
| **Custom (C1)** | `0x0066` | Toggle OSD visibility on/off |

#### Shell / Remote Control via `/tmp/gui.cmd`

You can also drive the OSD menu from a shell or script by writing commands to `/tmp/gui.cmd`:
```sh
echo down > /tmp/gui.cmd
echo right > /tmp/gui.cmd
echo enter > /tmp/gui.cmd
echo toggle > /tmp/gui.cmd
echo quit > /tmp/gui.cmd
```

---

## Building from Source

Requires [Zig](https://ziglang.org/) (`zig cc` cross-compiler):

```bat
cd gui
build.bat
```
