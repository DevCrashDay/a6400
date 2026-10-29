#!/bin/sh
# probe_focus.sh - on-camera test of script-driven focus moves.
#
# Run from the service shell with the camera in shooting mode (not playback,
# not a menu) and Focus Mode MF. Watch live view, ideally with Focus
# Magnifier or peaking on. Takes no pictures, changes no settings, writes
# nothing outside /tmp.
#
#   probe_focus.sh                      3 large moves near, then 3 far
#   probe_focus.sh near|far [SIZE] [N]  N moves (default 3) of SIZE
#                                       (small, medium, large or 1..7;
#                                       default large)

DIR=${0%/*}
[ "$DIR" = "$0" ] && DIR=.
SHUTTER_LIB=1 . "$DIR/shutter.sh"
. "$DIR/focus.sh"

say() {
    echo ""
    echo "==== $* ===="
}

# $1 = near|far, $2 = amount 1..7, $3 = count
moves() {
    _i=0
    while [ $_i -lt "$3" ]; do
        _i=$(( _i + 1 ))
        if focus_push "$1" "$2"; then
            echo "  move $_i/$3 $1 (size $2): sent"
        else
            echo "  move $_i/$3 $1 (size $2): FAILED (see error above)"
            return 1
        fi
        sleep 1
    done
    return 0
}

trap 'focus_release; exit 130' INT TERM

say "1. Sony test tool present"
if [ -x "$SNDCMD" ] || [ -n "$SHUTTER_DRYRUN" ]; then
    echo "ok      $SNDCMD"
else
    echo "MISSING $SNDCMD"
    exit 1
fi

case "$1" in
    near|far)
        if ! AMOUNT=$(focus_size "${2:-large}"); then
            echo "size must be small, medium, large or 1..7" >&2
            exit 2
        fi
        N=${3:-3}
        if ! shutter_is_uint "$N" || [ "$N" -lt 1 ]; then
            echo "count must be a whole number of at least 1" >&2
            exit 2
        fi
        say "2. $N move(s) $1, size $AMOUNT"
        moves "$1" "$AMOUNT" "$N" || exit 1
        echo ""
        echo "Did the focus move $1 in live view?"
        exit 0
        ;;
    '') ;;
    *)
        echo "usage: ${0##*/} [near|far [SIZE] [COUNT]]" >&2
        exit 2
        ;;
esac

say "2. 3 large moves NEAR (the focus plane should come closer)"
moves near 7 3 || exit 1
sleep 2

say "3. 3 large moves FAR (should end about where it started)"
moves far 7 3 || exit 1

say "Done"
echo "Moved near, then back far:  try 'probe_focus.sh far small 5' to see"
echo "  the smallest step, then run focus_bracket.sh."
echo "Moved the other way round:  tell me, the sign of the step is swapped."
echo "Nothing moved:              tell me; check Focus Mode is MF and live"
echo "  view is showing (not a menu or playback)."
echo "Anything else changed:      stop and report it."
exit 0
