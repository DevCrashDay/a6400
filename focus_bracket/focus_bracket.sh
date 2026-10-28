#!/bin/sh
# focus_bracket.sh - focus stacking bracket on the a6400.
#
# Shoots a series of frames while stepping the focus between each shot,
# for later stacking in Helicon Focus, Zerene Stacker, or similar software.
# Uses shutter.sh (same folder) for the shutter trigger and the InfraInput
# test hook to inject STEP_FOCUS_NEAR / STEP_FOCUS_FAR virtual keys.
#
# Prerequisites:
#   - Camera in MF mode (so AF does not override the focus steps).
#   - Lens focused manually on the nearest (or farthest) subject.
#   - Shutter speed, aperture, ISO set as desired (M or A mode recommended).
#   - Long Exposure NR off (avoids double-length waits between frames).
#   - Run probe_focus.sh first to confirm focus stepping works.
#
# Usage:
#   focus_bracket.sh [-n COUNT] [-s STEPS] [-d DIR] [-w WAIT] [-i INTERVAL]
#                    [--settle SETTLE] [--bg]
#   focus_bracket.sh stop       finish the current frame, then stop
#   focus_bracket.sh abort      stop immediately
#   focus_bracket.sh status     show whether a run is active and its log
#
#   -n COUNT     number of frames to shoot                      (default 10)
#   -s STEPS     focus steps between each frame; more = bigger
#                focus shift per frame, fewer = finer slices    (default 2)
#   -d DIR       direction to step: "far" or "near"            (default far)
#   -w WAIT      seconds after stepping before taking the shot,
#                lets the lens settle and live view refresh     (default 2)
#   -i INTERVAL  minimum seconds between frames (start to start);
#                the script always waits at least WAIT after the
#                last step, so this only matters when the camera
#                shutter speed is long                          (default 5)
#   --settle SETTLE  milliseconds to pause between individual
#                focus key events (integer; default 200, i.e. 0.2 s)
#   --bg         run in the background (log to /tmp/focus.log)
#
# Examples:
#   focus_bracket.sh -n 30 -s 1 -d far          30 fine slices, near to far
#   focus_bracket.sh -n 20 -s 5 -d near --bg    20 coarse slices, far to near

DIR_SCRIPT=${0%/*}
[ "$DIR_SCRIPT" = "$0" ] && DIR_SCRIPT=.
SHUTTER_LIB=1 . "$DIR_SCRIPT/shutter.sh"

LOG=/tmp/focus.log
PIDFILE=/tmp/focus.pid
STOPFILE=/tmp/focus.stop

INFRA_INPUT_TESTCMD=0x007A018C
INFRA_INPUT_CMD_SEND_EVENT=2
KEY_STEP_FOCUS_NEAR=0x1A000029
KEY_STEP_FOCUS_FAR=0x1A00002A

COUNT=10
STEPS=2
DIRECTION=far
WAIT=2
INTERVAL=5
SETTLE=200
BG=

log() {
    echo "[focus +$(( $(now) - START ))s] $*"
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

wait_until() {
    while :; do
        [ -f "$STOPFILE" ] && return 1
        _rem=$(( $1 - $(now) ))
        [ $_rem -le 0 ] && return 0
        [ $_rem -gt 5 ] && _rem=5
        sleep $_rem
    done
}

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

focus_key() {
    if [ -n "$SHUTTER_DRYRUN" ]; then
        echo "[dry-run] $SNDCMD --sync $INFRA_INPUT_TESTCMD d:$INFRA_INPUT_CMD_SEND_EVENT d:$1 d:0 d:$2 d:0"
        return 0
    fi
    _out=$("$SNDCMD" --sync $INFRA_INPUT_TESTCMD d:$INFRA_INPUT_CMD_SEND_EVENT d:$1 d:0 d:$2 d:0 2>&1)
    _rc=$?
    if [ $_rc -ne 0 ]; then
        echo "focus: sndcmd failed (rc=$_rc) for key $1 state $2: $_out" >&2
        return $_rc
    fi
    return 0
}

focus_step_once() {
    _key=$1
    focus_key "$_key" 1 || return $?
    msleep
    focus_key "$_key" 0
}

msleep() {
    if [ "$SETTLE" -ge 1000 ]; then
        sleep $(( SETTLE / 1000 ))
    else
        sleep 1
    fi
}

focus_step() {
    _count=$1
    _key=$2
    _i=0
    while [ $_i -lt "$_count" ]; do
        focus_step_once "$_key" || return $?
        _i=$(( _i + 1 ))
        [ $_i -lt "$_count" ] && msleep
    done
    return 0
}

need_uint() {
    case "$2" in
        ''|*[!0-9]*) echo "focus_bracket: $1 needs a whole number, got '$2'" >&2; exit 2 ;;
    esac
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
        -s) need_uint -s "$2"; STEPS=$2; shift 2 ;;
        -d)
            case "$2" in
                near|far) DIRECTION=$2 ;;
                *) echo "focus_bracket: -d must be 'near' or 'far', got '$2'" >&2; exit 2 ;;
            esac
            shift 2
            ;;
        -w) need_uint -w "$2"; WAIT=$2; shift 2 ;;
        -i) need_uint -i "$2"; INTERVAL=$2; shift 2 ;;
        --settle) need_uint --settle "$2"; SETTLE=$2; shift 2 ;;
        --bg) BG=1; shift ;;
        -h|--help)
            sed -n '2,40p' "$0"
            exit 0
            ;;
        *)
            echo "focus_bracket: unknown option '$1' (try -h)" >&2
            exit 2
            ;;
    esac
done

if [ "$COUNT" -lt 2 ]; then
    echo "focus_bracket: need at least 2 frames for a bracket (-n)" >&2
    exit 2
fi
if [ "$STEPS" -lt 1 ]; then
    echo "focus_bracket: need at least 1 step between frames (-s)" >&2
    exit 2
fi

if [ -n "$BG" ]; then
    rm -f "$STOPFILE"
    sh "$0" -n "$COUNT" -s "$STEPS" -d "$DIRECTION" -w "$WAIT" -i "$INTERVAL" --settle "$SETTLE" < /dev/null > "$LOG" 2>&1 &
    echo "started in background (pid $!), log: $LOG"
    echo "stop with: $0 stop   (or '$0 abort' to cut short)"
    exit 0
fi

if [ ! -x "$SNDCMD" ] && [ -z "$SHUTTER_DRYRUN" ]; then
    echo "focus_bracket: $SNDCMD not found or not executable" >&2
    exit 2
fi

if _p=$(running_pid); then
    echo "focus_bracket: already running (pid $_p); use '$0 stop' first" >&2
    exit 1
fi

if [ "$DIRECTION" = "far" ]; then
    FOCUS_KEY=$KEY_STEP_FOCUS_FAR
else
    FOCUS_KEY=$KEY_STEP_FOCUS_NEAR
fi

START=$(now)
echo $$ > "$PIDFILE"
rm -f "$STOPFILE"
trap 'shutter_release; rm -f "$PIDFILE"; log "aborted"; exit 130' INT TERM
trap '' HUP

CLOCK=1
[ "$START" = 0 ] && CLOCK=

log "start: $COUNT frames, $STEPS step(s) between frames, direction=$DIRECTION, wait=${WAIT}s"
log "total focus travel: $(( (COUNT - 1) * STEPS )) steps $DIRECTION"
[ -z "$CLOCK" ] && log "clock unavailable, timing falls back to plain sleeps"

FRAME=0
FAILS=0
while [ $FRAME -lt "$COUNT" ]; do
    [ -f "$STOPFILE" ] && break
    FRAME=$(( FRAME + 1 ))
    FRAME_START=$(now)

    if [ $FRAME -gt 1 ]; then
        log "frame $FRAME/$COUNT: stepping $STEPS $DIRECTION"
        if ! focus_step "$STEPS" "$FOCUS_KEY"; then
            FAILS=$(( FAILS + 1 ))
            log "frame $FRAME: focus step failed ($FAILS in a row)"
            if [ $FAILS -ge 3 ]; then
                log "giving up after 3 failures"
                break
            fi
            continue
        fi
        log "frame $FRAME/$COUNT: waiting ${WAIT}s for lens to settle"
        if [ -n "$CLOCK" ]; then
            wait_until $(( FRAME_START + WAIT )) || break
        else
            wait_seconds "$WAIT" || break
        fi
    fi

    log "frame $FRAME/$COUNT: shot"
    if shutter_shot; then
        FAILS=0
    else
        FAILS=$(( FAILS + 1 ))
        log "frame $FRAME: shutter failed ($FAILS in a row)"
        if [ $FAILS -ge 3 ]; then
            log "giving up after 3 failures"
            shutter_release
            break
        fi
    fi

    if [ $FRAME -lt "$COUNT" ] && [ -n "$CLOCK" ]; then
        _target=$(( FRAME_START + INTERVAL ))
        _now=$(now)
        if [ $_now -lt "$_target" ]; then
            log "frame $FRAME/$COUNT: pacing ($(( _target - _now ))s until next)"
            wait_until "$_target" || break
        fi
    elif [ $FRAME -lt "$COUNT" ] && [ -z "$CLOCK" ]; then
        wait_seconds "$INTERVAL" || break
    fi
done

log "done: $FRAME frames shot, $(( (FRAME - 1) * STEPS )) total steps $DIRECTION"
rm -f "$PIDFILE" "$STOPFILE"
exit 0
