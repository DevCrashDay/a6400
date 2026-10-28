#!/bin/sh
# probe_focus.sh - first on-camera test of script-driven focus steps.
#
# Run from the service shell with the camera in shooting mode (not playback,
# not a menu) and focus mode MF. Watch live view, ideally with Focus
# Magnifier or peaking on, while it runs. Takes no pictures, changes no
# settings, writes nothing outside /tmp.
#
#   probe_focus.sh              3 steps near, pause, 3 steps far
#   probe_focus.sh near [N]     N steps near (default 3)
#   probe_focus.sh far [N]      N steps far (default 3)

DIR=${0%/*}
[ "$DIR" = "$0" ] && DIR=.
SHUTTER_LIB=1 . "$DIR/shutter.sh"

KEY_STEP_FOCUS_NEAR=0x1A00002F
KEY_STEP_FOCUS_FAR=0x1A000030

say() {
    echo ""
    echo "==== $* ===="
}

# $1 = near|far, $2 = count. Each step is a press and an immediate release.
steps() {
    if [ "$1" = near ]; then _k=$KEY_STEP_FOCUS_NEAR; else _k=$KEY_STEP_FOCUS_FAR; fi
    _i=0
    while [ $_i -lt "$2" ]; do
        _i=$(( _i + 1 ))
        if shutter_key $_k 1 && shutter_key $_k 0; then
            echo "  step $_i/$2 $1: sent"
        else
            echo "  step $_i/$2 $1: FAILED (see error above)"
            shutter_key $_k 0
            return 1
        fi
        sleep 1
    done
    return 0
}

trap 'shutter_key $KEY_STEP_FOCUS_NEAR 0; shutter_key $KEY_STEP_FOCUS_FAR 0; exit 130' INT TERM

say "1. Sony test tool present"
if [ -x "$SNDCMD" ] || [ -n "$SHUTTER_DRYRUN" ]; then
    echo "ok      $SNDCMD"
else
    echo "MISSING $SNDCMD"
    exit 1
fi

case "$1" in
    near|far)
        N=${2:-3}
        if ! shutter_is_uint "$N" || [ "$N" -lt 1 ]; then
            echo "count must be a whole number of at least 1" >&2
            exit 2
        fi
        say "2. $N step(s) $1"
        steps "$1" "$N" || exit 1
        echo ""
        echo "Did the focus move $1 in live view?"
        exit 0
        ;;
    '') ;;
    *)
        echo "usage: ${0##*/} [near|far [COUNT]]" >&2
        exit 2
        ;;
esac

say "2. 3 steps NEAR (watch the focus plane come closer)"
steps near 3 || exit 1
sleep 2

say "3. 3 steps FAR (should end about where it started)"
steps far 3 || exit 1

say "Done"
echo "Focus moved near, then back far:  focus_bracket.sh is ready to try."
echo "Moved, but by a lot per step:      use -s 1 and check the overlap."
echo "Nothing moved, every send said ok: check MF is on and live view is"
echo "  showing. If so the camera may ignore remote keys with no remote"
echo "  connected; turn on Ctrl w/ Smartphone, connect a phone once, retry."
echo "Something else changed (WB lock, a mode switch): stop and report it,"
echo "  the key codes would be wrong."
exit 0
