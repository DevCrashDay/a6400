#!/bin/sh
# focus_bracket.sh - focus stacking bracket on the a6400.
#
# Shoots a series of frames, stepping the focus between shots, for stacking
# in Helicon Focus, Zerene Stacker, Photoshop and the like. Uses shutter.sh
# (same folder) to inject the Wi-Fi remote's STEP_FOCUS_NEAR / STEP_FOCUS_FAR
# keys and the S1/S2 shutter keys through the InfraInput test hook.
#
# Before running:
#   - Run probe_focus.sh once and confirm the focus moves in live view.
#   - Focus mode MF, lens focused on the nearest point (for -d far) or the
#     farthest point (for -d near).
#   - M or A mode, Long Exposure NR off, camera on a tripod.
#
# Usage:
#   focus_bracket.sh [-n COUNT] [-s STEPS] [-d far|near] [-w WAIT]
#                    [-g GAP] [-i INTERVAL] [--bg]
#   focus_bracket.sh stop       finish the current frame, then stop
#   focus_bracket.sh abort      stop now and release all keys
#   focus_bracket.sh status     show whether a run is active and its log
#
#   -n COUNT     frames to shoot, at least 2                     (default 10)
#   -s STEPS     focus steps between frames; more = bigger slice  (default 2)
#   -d DIR       far = start near, move away; near = the reverse  (default far)
#   -w WAIT      whole seconds to wait after the last step before
#                the shot, so the lens stops moving               (default 2)
#   -g GAP       whole seconds between single steps, 0 = none     (default 1)
#   -i INTERVAL  minimum seconds from one frame's start to the
#                next; raise it for long shutter speeds           (default 5)
#   --bg         run in the background, log to /tmp/focus.log
#
# Examples:
#   focus_bracket.sh -n 30 -s 1 -d far          30 thin slices, near to far
#   focus_bracket.sh -n 20 -s 5 -d near --bg    20 thick slices, far to near
#
# BusyBox 1.13.4 on the camera: integer sleep only, no printf/getopts/awk.

DIR=${0%/*}
[ "$DIR" = "$0" ] && DIR=.
SHUTTER_LIB=1 . "$DIR/shutter.sh"

KEY_STEP_FOCUS_NEAR=0x1A00002F
KEY_STEP_FOCUS_FAR=0x1A000030

LOG=/tmp/focus.log
PIDFILE=/tmp/focus.pid
STOPFILE=/tmp/focus.stop

COUNT=10
STEPS=2
DIRECTION=far
WAIT=2
GAP=1
INTERVAL=5
BG=

now() {
    _t=$(busybox date +%s 2>/dev/null)
    case "$_t" in
        ''|*[!0-9]*) echo 0 ;;
        *) echo "$_t" ;;
    esac
}

log() {
    echo "[focus +$(( $(now) - START ))s] $*"
}

running_pid() {
    [ -f "$PIDFILE" ] || return 1
    _p=$(cat "$PIDFILE")
    [ -n "$_p" ] && [ -d "/proc/$_p" ] && echo "$_p"
}

# Sleep in chunks of at most 5 s so a stop request is noticed.
# Returns 1 if a stop was requested.
wait_seconds() {
    _left=$1
    while [ "$_left" -gt 0 ]; do
        [ -f "$STOPFILE" ] && return 1
        _chunk=$_left
        [ $_chunk -gt 5 ] && _chunk=5
        sleep $_chunk
        _left=$(( _left - _chunk ))
    done
    [ -f "$STOPFILE" ] && return 1
    return 0
}

release_all() {
    shutter_key $KEY_STEP_FOCUS_NEAR 0
    shutter_key $KEY_STEP_FOCUS_FAR 0
    shutter_release
}

# One step = press and release right away. Holding the key longer could
# trigger InfraInput key repeat and move the lens more than one step.
focus_step() {
    _i=0
    while [ $_i -lt "$1" ]; do
        [ $_i -gt 0 ] && [ "$GAP" -gt 0 ] && sleep "$GAP"
        shutter_key "$FOCUS_KEY" 1 || return 1
        shutter_key "$FOCUS_KEY" 0 || return 1
        _i=$(( _i + 1 ))
    done
    return 0
}

need_uint() {
    if ! shutter_is_uint "$2"; then
        echo "focus_bracket: $1 needs a whole number, got '$2'" >&2
        exit 2
    fi
}

case "$1" in
    stop)
        echo > "$STOPFILE"
        if _p=$(running_pid); then
            echo "stop requested; run (pid $_p) ends after the current frame"
        else
            echo "no run active"
        fi
        exit 0
        ;;
    abort)
        echo > "$STOPFILE"
        if _p=$(running_pid); then
            kill "$_p"
            echo "aborted pid $_p"
        fi
        release_all
        exit 0
        ;;
    status)
        if _p=$(running_pid); then
            echo "running (pid $_p)"
        else
            echo "not running"
        fi
        [ -f "$LOG" ] && cat "$LOG"
        exit 0
        ;;
esac

while [ $# -gt 0 ]; do
    case "$1" in
        -n) need_uint -n "$2"; COUNT=$2; shift 2 ;;
        -s) need_uint -s "$2"; STEPS=$2; shift 2 ;;
        -w) need_uint -w "$2"; WAIT=$2; shift 2 ;;
        -g) need_uint -g "$2"; GAP=$2; shift 2 ;;
        -i) need_uint -i "$2"; INTERVAL=$2; shift 2 ;;
        -d)
            case "$2" in
                near|far) DIRECTION=$2 ;;
                *) echo "focus_bracket: -d must be 'near' or 'far', got '$2'" >&2; exit 2 ;;
            esac
            shift 2
            ;;
        --bg) BG=1; shift ;;
        -h|--help)
            sed -n '2,33p' "$0"
            exit 0
            ;;
        *)
            echo "focus_bracket: unknown option '$1' (try -h)" >&2
            exit 2
            ;;
    esac
done

if [ "$COUNT" -lt 2 ]; then
    echo "focus_bracket: -n must be at least 2" >&2
    exit 2
fi
if [ "$STEPS" -lt 1 ]; then
    echo "focus_bracket: -s must be at least 1" >&2
    exit 2
fi
if [ ! -x "$SNDCMD" ] && [ -z "$SHUTTER_DRYRUN" ]; then
    echo "focus_bracket: $SNDCMD not found or not executable" >&2
    exit 2
fi
if _p=$(running_pid); then
    echo "focus_bracket: already running (pid $_p); use '$0 stop' first" >&2
    exit 1
fi

if [ -n "$BG" ]; then
    rm -f "$STOPFILE"
    sh "$0" -n "$COUNT" -s "$STEPS" -d "$DIRECTION" -w "$WAIT" -g "$GAP" -i "$INTERVAL" < /dev/null > "$LOG" 2>&1 &
    echo "started in background (pid $!), log: $LOG"
    echo "stop with: $0 stop   (or '$0 abort' to stop at once)"
    exit 0
fi

if [ "$DIRECTION" = far ]; then
    FOCUS_KEY=$KEY_STEP_FOCUS_FAR
else
    FOCUS_KEY=$KEY_STEP_FOCUS_NEAR
fi

START=$(now)
echo $$ > "$PIDFILE"
rm -f "$STOPFILE"
trap 'release_all; rm -f "$PIDFILE"; log "aborted"; exit 130' INT TERM
trap '' HUP

CLOCK=1
[ "$START" = 0 ] && CLOCK=

log "start: $COUNT frames, $STEPS step(s) $DIRECTION between frames, wait ${WAIT}s, gap ${GAP}s, interval ${INTERVAL}s"
[ -z "$CLOCK" ] && log "clock unavailable, using plain sleeps for pacing"

FRAME=0
SHOT=0
STEPPED=0
while [ $FRAME -lt "$COUNT" ]; do
    [ -f "$STOPFILE" ] && { log "stop requested"; break; }
    FRAME=$(( FRAME + 1 ))
    FRAME_START=$(now)

    if [ $FRAME -gt 1 ]; then
        log "frame $FRAME/$COUNT: $STEPS step(s) $DIRECTION"
        if ! focus_step "$STEPS"; then
            # The lens position is unknown now, so more frames would be useless.
            log "focus step failed after $STEPPED step(s); stopping"
            release_all
            break
        fi
        STEPPED=$(( STEPPED + STEPS ))
        wait_seconds "$WAIT" || { log "stop requested"; break; }
    fi

    log "frame $FRAME/$COUNT: shot"
    if shutter_shot; then
        SHOT=$(( SHOT + 1 ))
    else
        log "shutter failed on frame $FRAME; stopping"
        release_all
        break
    fi

    [ $FRAME -ge "$COUNT" ] && break
    if [ -n "$CLOCK" ]; then
        _rem=$(( FRAME_START + INTERVAL - $(now) ))
    else
        _rem=$INTERVAL
    fi
    if [ $_rem -gt 0 ]; then
        wait_seconds $_rem || { log "stop requested"; break; }
    fi
done

log "done: $SHOT frame(s) shot, focus moved $STEPPED step(s) $DIRECTION"
rm -f "$PIDFILE" "$STOPFILE"
exit 0
