#!/bin/sh
# probe_focus.sh - test focus stepping on the a6400 without taking pictures.
#
# Run from the service shell with the camera in shooting mode and MF selected.
# Watch the live view while it runs: the focus plane should visibly shift.
#
#   probe_focus.sh              step near 3x, pause, step far 3x
#   probe_focus.sh near [N]     step near N times (default 3)
#   probe_focus.sh far  [N]     step far N times (default 3)
#   probe_focus.sh drive        hold continuous focus drive near for 2 s
#
# It does not take any pictures and writes nothing outside /tmp.

DIR=${0%/*}
[ "$DIR" = "$0" ] && DIR=.
SHUTTER_LIB=1 . "$DIR/shutter.sh"

INFRA_INPUT_TESTCMD=0x007A018C
INFRA_INPUT_CMD_SEND_EVENT=2
KEY_STEP_FOCUS_NEAR=0x1A000029
KEY_STEP_FOCUS_FAR=0x1A00002A
KEY_FOCUS_DRIVE_NEAR_FAR=0x1A00001F

step_msg() {
    echo ""
    echo "==== $* ===="
}

focus_key() {
    if [ -n "$SHUTTER_DRYRUN" ]; then
        echo "[dry-run] $SNDCMD --sync $INFRA_INPUT_TESTCMD d:$INFRA_INPUT_CMD_SEND_EVENT d:$1 d:0 d:$2 d:0"
        return 0
    fi
    _out=$("$SNDCMD" --sync $INFRA_INPUT_TESTCMD d:$INFRA_INPUT_CMD_SEND_EVENT d:$1 d:0 d:$2 d:0 2>&1)
    _rc=$?
    if [ $_rc -ne 0 ]; then
        echo "probe_focus: sndcmd failed (rc=$_rc): $_out" >&2
        return $_rc
    fi
    return 0
}

is_uint() {
    case "$1" in
        ''|*[!0-9]*) return 1 ;;
    esac
    return 0
}

step_msg "1. Check sndcmd"
if [ ! -x "$SNDCMD" ] && [ -z "$SHUTTER_DRYRUN" ]; then
    echo "MISSING $SNDCMD"
    exit 1
fi
echo "ok      $SNDCMD"

case "$1" in
    near|far)
        N=${2:-3}
        is_uint "$N" || { echo "count must be a whole number" >&2; exit 2; }
        KEY=$KEY_STEP_FOCUS_NEAR
        [ "$1" = "far" ] && KEY=$KEY_STEP_FOCUS_FAR
        step_msg "2. Stepping $1 x$N"
        echo "Watch live view: the focus plane should shift $1."
        _i=0
        while [ $_i -lt "$N" ]; do
            _i=$(( _i + 1 ))
            echo "  step $_i/$N: press"
            focus_key "$KEY" 1 || { echo "FAILED on step $_i press"; exit 1; }
            sleep 1
            echo "  step $_i/$N: release"
            focus_key "$KEY" 0 || { echo "FAILED on step $_i release"; exit 1; }
            sleep 1
        done
        step_msg "Done"
        echo "Did the focus visibly shift $1 in live view?"
        exit 0
        ;;
    drive)
        step_msg "2. Continuous focus drive near (2 s hold)"
        echo "Watch live view: focus should ramp toward near."
        echo "  key press (drive start)"
        focus_key "$KEY_FOCUS_DRIVE_NEAR_FAR" 1 || { echo "FAILED on drive press"; exit 1; }
        sleep 2
        echo "  key release (drive stop)"
        focus_key "$KEY_FOCUS_DRIVE_NEAR_FAR" 0 || { echo "FAILED on drive release"; exit 1; }
        step_msg "Done"
        echo "Did the focus ramp toward near?"
        echo "Note: FOCUS_DRIVE_NEAR_FAR (0x1A00001F) may need a sub_key_code"
        echo "for direction. If nothing happened, the step keys are the safer path."
        exit 0
        ;;
esac

step_msg "2. Step focus NEAR x3"
echo "Watch live view: the focus plane should shift toward near."
_i=0
while [ $_i -lt 3 ]; do
    _i=$(( _i + 1 ))
    echo "  step $_i/3: press"
    focus_key "$KEY_STEP_FOCUS_NEAR" 1
    sleep 1
    echo "  step $_i/3: release"
    focus_key "$KEY_STEP_FOCUS_NEAR" 0
    sleep 1
done
sleep 2

step_msg "3. Step focus FAR x3 (should return to roughly where we started)"
echo "Watch live view: the focus plane should shift toward far."
_i=0
while [ $_i -lt 3 ]; do
    _i=$(( _i + 1 ))
    echo "  step $_i/3: press"
    focus_key "$KEY_STEP_FOCUS_FAR" 1
    sleep 1
    echo "  step $_i/3: release"
    focus_key "$KEY_STEP_FOCUS_FAR" 0
    sleep 1
done

step_msg "Done"
echo ""
echo "Results:"
echo "  - If focus shifted near then back far: STEP keys work, focus_bracket.sh is ready."
echo "  - If focus shifted only one direction: the other key may need a different code."
echo "  - If nothing happened but sends said OK: camera may inhibit remote focus keys."
echo "    Try enabling Ctrl w/ Smartphone, let a phone connect once, then retry."
echo "    Also confirm camera is in MF mode (AF may override stepped focus)."
exit 0
