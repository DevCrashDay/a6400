#!/bin/sh
# intervalometer.sh - interval shooting and long bulb exposures on the a6400.
#
# Uses shutter.sh (same folder) to press the shutter via the camera's own
# input test hook. No Wi-Fi, phone or cable needed.
#
# Usage:
#   intervalometer.sh [-n COUNT] [-i INTERVAL] [-b BULB] [-d DELAY] [--bg]
#   intervalometer.sh stop      finish the current frame, then stop
#   intervalometer.sh abort     stop right now (ends a running bulb exposure)
#   intervalometer.sh status    show whether a run is active and its log
#
#   -n COUNT     number of frames, 0 = until stopped            (default 10)
#   -i INTERVAL  seconds from the start of one frame to the next
#                (default: BULB + 5 in bulb mode, otherwise 10)
#   -b BULB      bulb exposure in seconds. Set the camera to M and turn the
#                shutter speed past 30" to BULB first. Leave -b out to use
#                whatever shutter speed is set on the camera (up to 30s).
#   -d DELAY     wait DELAY seconds before the first frame       (default 0)
#   --bg         run in the background, log to /tmp/astro.log, survive the
#                service shell closing
#
# Examples:
#   intervalometer.sh -b 120 -n 30 -i 125 --bg   30 frames of 2 minutes each
#   intervalometer.sh -n 300 -i 20 --bg          star trails at camera's 15"
#
# Turn Long Exposure NR off for interval work: it adds a dark frame as long
# as the exposure, during which the camera ignores the shutter.

DIR=${0%/*}
[ "$DIR" = "$0" ] && DIR=.
SHUTTER_LIB=1 . "$DIR/shutter.sh"

LOG=/tmp/astro.log
PIDFILE=/tmp/astro.pid
STOPFILE=/tmp/astro.stop

COUNT=10
INTERVAL=
BULB=
DELAY=0
BG=

log() {
    echo "[astro +$(( $(now) - START ))s] $*"
}

now() {
    _t=$(busybox date +%s 2>/dev/null)
    case "$_t" in
        ''|*[!0-9]*) echo 0 ;;
        *) echo "$_t" ;;
    esac
}

running_pid() {
    [ -f "$PIDFILE" ] || return 1
    _p=$(cat "$PIDFILE")
    [ -n "$_p" ] && [ -d "/proc/$_p" ] && echo "$_p"
}

# Sleep until the clock reaches $1, waking every few seconds to honour the
# stop file. Returns 1 if a stop was requested.
wait_until() {
    while :; do
        [ -f "$STOPFILE" ] && return 1
        _rem=$(( $1 - $(now) ))
        [ $_rem -le 0 ] && return 0
        [ $_rem -gt 5 ] && _rem=5
        sleep $_rem
    done
}

# Fallback when the clock is unusable: sleep in small steps.
wait_seconds() {
    _left=$1
    while [ $_left -gt 0 ]; do
        [ -f "$STOPFILE" ] && return 1
        _step=$_left
        [ $_step -gt 5 ] && _step=5
        sleep $_step
        _left=$(( _left - _step ))
    done
    return 0
}

need_uint() {
    if ! shutter_is_uint "$2"; then
        echo "intervalometer: $1 needs a whole number, got '$2'" >&2
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
        shutter_release
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
        -i) need_uint -i "$2"; INTERVAL=$2; shift 2 ;;
        -b) need_uint -b "$2"; BULB=$2; shift 2 ;;
        -d) need_uint -d "$2"; DELAY=$2; shift 2 ;;
        --bg) BG=1; shift ;;
        -h|--help)
            sed -n '2,30p' "$0"
            exit 0
            ;;
        *)
            echo "intervalometer: unknown option '$1' (try -h)" >&2
            exit 2
            ;;
    esac
done

if [ -n "$BULB" ] && [ "$BULB" -lt 1 ]; then
    echo "intervalometer: -b must be at least 1 second" >&2
    exit 2
fi
if [ -z "$INTERVAL" ]; then
    if [ -n "$BULB" ]; then INTERVAL=$(( BULB + 5 )); else INTERVAL=10; fi
fi
if [ -n "$BULB" ] && [ "$INTERVAL" -lt $(( BULB + 2 )) ]; then
    echo "intervalometer: interval ($INTERVAL s) must be at least bulb + 2 s so the camera can save the frame" >&2
    exit 2
fi
if [ "$INTERVAL" -lt 1 ]; then
    echo "intervalometer: interval must be at least 1 second" >&2
    exit 2
fi

if _p=$(running_pid); then
    echo "intervalometer: already running (pid $_p); use '$0 stop' first" >&2
    exit 1
fi

if [ ! -x "$SNDCMD" ] && [ -z "$SHUTTER_DRYRUN" ]; then
    echo "intervalometer: $SNDCMD not found or not executable" >&2
    exit 2
fi

if [ -n "$BG" ]; then
    rm -f "$STOPFILE"
    if [ -n "$BULB" ]; then
        sh "$0" -n "$COUNT" -i "$INTERVAL" -d "$DELAY" -b "$BULB" < /dev/null > "$LOG" 2>&1 &
    else
        sh "$0" -n "$COUNT" -i "$INTERVAL" -d "$DELAY" < /dev/null > "$LOG" 2>&1 &
    fi
    echo "started in background (pid $!), log: $LOG"
    echo "stop with: $0 stop   (or '$0 abort' to cut a bulb exposure short)"
    exit 0
fi

START=$(now)
echo $$ > "$PIDFILE"
rm -f "$STOPFILE"
trap 'shutter_release; rm -f "$PIDFILE"; log "aborted"; exit 130' INT TERM
trap '' HUP

CLOCK=1
[ "$START" = 0 ] && CLOCK=

if [ -n "$BULB" ]; then
    MODE="bulb ${BULB}s"
else
    MODE="camera shutter speed"
fi
if [ "$COUNT" = 0 ]; then TOTAL=unlimited; else TOTAL=$COUNT; fi
log "start: $TOTAL frames, $MODE, every ${INTERVAL}s, delay ${DELAY}s"
[ -z "$CLOCK" ] && log "clock unavailable, timing falls back to plain sleeps"

if [ "$DELAY" -gt 0 ]; then
    if [ -n "$CLOCK" ]; then
        wait_until $(( START + DELAY ))
    else
        wait_seconds "$DELAY"
    fi || { log "stopped during delay"; rm -f "$PIDFILE" "$STOPFILE"; exit 0; }
fi

T0=$(now)
FRAME=0
FAILS=0
while [ "$COUNT" = 0 ] || [ $FRAME -lt "$COUNT" ]; do
    [ -f "$STOPFILE" ] && break
    FRAME=$(( FRAME + 1 ))
    if [ -n "$BULB" ]; then
        log "frame $FRAME/$TOTAL: bulb ${BULB}s"
        shutter_bulb "$BULB"
    else
        log "frame $FRAME/$TOTAL: shot"
        shutter_shot
    fi
    if [ $? -ne 0 ]; then
        FAILS=$(( FAILS + 1 ))
        log "frame $FRAME: key injection failed ($FAILS in a row)"
        if [ $FAILS -ge 3 ]; then
            log "giving up after 3 failures"
            shutter_release
            break
        fi
    else
        FAILS=0
    fi
    [ "$COUNT" != 0 ] && [ $FRAME -ge "$COUNT" ] && break
    if [ -n "$CLOCK" ]; then
        wait_until $(( T0 + FRAME * INTERVAL )) || break
    elif [ -n "$BULB" ]; then
        wait_seconds $(( INTERVAL - BULB )) || break
    else
        wait_seconds "$INTERVAL" || break
    fi
done

log "done: $FRAME frames"
rm -f "$PIDFILE" "$STOPFILE"
exit 0
