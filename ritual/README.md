# The Ritual (film engine and helpers)

Cross-compiled ARMv7 programs for the a6400's Linux side, plus their sources.
`hook/autoexec.sh` starts `ritual_daemon` and `watchdog.sh` at boot from the
camera paths `/tmp/ritual_daemon` and `/mnt/watchdog.sh` (SD card root), so
copy them there; this folder is only where they live in the repo.

| File | What it is |
|------|------------|
| `ritual_daemon` / `.c` | Film lab daemon: processes new shots on SD using the film stock in `RITUAL.ini` (ARW/MPF preview extraction). |
| `ritual_menu` / `.c` | Terminal menu for the service shell. |
| `arw_develop` / `.c` | ARW developer used by the daemon. |
| `star_trails` / `.c` | Star-trail stacker (stb_image based). |
| `timelapse_mux` / `.c` | Muxes a JPEG sequence into an MJPEG AVI time-lapse. |
| `sony_ptpip` / `.c` | PTP/IP client for the camera's own server on localhost:15740. |
| `watchdog.sh` | Restarts the daemon if it dies (BusyBox 1.13 safe). |

Status: none of these are confirmed on camera yet except where
`PROJECT_MEMORY.md` says so. Known issue: SD edits to `RITUAL.ini` lose to
`/tmp/RITUAL.ini` (see Open Issues in `PROJECT_MEMORY.md`).
