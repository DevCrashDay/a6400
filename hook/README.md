# hook/ — boot persistence via LD_PRELOAD

Makes the camera run `/tmp/autoexec.sh` automatically at every power-on,
without modifying firmware. A patched `libAK8975.so` is loaded via
`/etc/ld.so.preload` as the earliest user-space hook.

**Status:** confirmed working via real cold boot (2026-10-28). The hook
fires, `autoexec.sh` runs, and homebrew services start unattended.

| File | What it does |
|---|---|
| `autoexec_hook.so` | Patched `libAK8975.so` with an ARM constructor that runs `autoexec.sh` |
| `autoexec.sh` | Startup script: mounts SD, syncs binaries, starts telnet/httpd, launches services |
| `build_autoexec_hook.py` | Python script that patches `libAK8975.so` to add the DT_INIT constructor |
| `libAK8975.so` | Original unpatched library (AKM compass driver, unused on a6400) |
| `check.sh` | Diagnostic script: checks mounts, hook files, services, processes |
| `verify-after-boot.sh` | Run after a power-cycle to confirm the hook fired at boot |

## How it works

1. `/etc/ld.so.preload` lists `autoexec_hook.so` (or its `/tmp` path).
2. The dynamic linker loads it into every new process.
3. The patched `DT_INIT` constructor runs once: checks killswitches, grabs
   an `O_EXCL` lock at `/tmp/autoexec.lock`, double-forks, and `execve`s
   `/bin/sh /tmp/autoexec.sh`.
4. `autoexec.sh` cleans up the lock on exit (critical — `/tmp` is persistent
   flash, not tmpfs, so a stale lock would block the next boot).

## Installing

1. Start the PMCA service shell
   (`pmca-console-v0.18-win.exe serviceshell` in your project folder).
2. Push the files (from the PMCA prompt, before `shell`):
   ```
   push hook\autoexec_hook.so /tmp/autoexec_hook.so
   push hook\autoexec.sh /tmp/autoexec.sh
   ```
3. Enter the shell and install the preload:
   ```
   shell
   chmod 755 /tmp/autoexec.sh
   echo /tmp/autoexec_hook.so > /etc/ld.so.preload
   ```
4. Power-cycle the camera. On the next boot, `autoexec.sh` runs automatically.

## Killswitches

Create any of these files to disable the hook:

- `/mnt/disable_hook` or `/mnt/disable.txt` on the SD card
- `/tmp/disable_hook` on the camera's flash

When a killswitch is detected, `autoexec.sh` removes `/etc/ld.so.preload`
and exits, fully uninstalling the hook.

## Safety

- The hook only modifies `/etc/ld.so.preload` (one line) and writes to `/tmp`.
- Killswitch files on the SD card give a hardware-level way to disable it.
- The lock-cleanup trap prevents the persistent `/tmp` lock from bricking
  the hook on subsequent boots.
- `libAK8975.so` is an unused compass driver on the a6400 (no magnetometer),
  so replacing its init function has no side effects.
