#!/bin/sh
# focus.sh - move the a6400's focus from a script. Library, source it:
#   SHUTTER_LIB=1 . shutter.sh; . focus.sh
#
# Uses the same path as the camera's USB PC remote (PTP control 0xD2D1
# "Near/Far", see InfraRemoteUsb::actionControlDevice in libInfraRemote.so):
# key FOCUS_DRIVE_NEAR_FAR (0x1A000025) pressed and released, with a signed
# step amount in the event's ex_info field. Negative = near, positive = far.
# Sony uses 1 / 3 / 7 for small / medium / large steps.
# Needs focus mode MF and a lens the camera can drive.

KEY_FOCUS_DRIVE=0x1A000025

# $1 = near|far, $2 = amount 1..7. Prints the 32-bit ex_info value.
focus_amount() {
    if [ "$1" = far ]; then
        echo "$2"
        return 0
    fi
    case "$2" in
        1) echo 0xFFFFFFFF ;;
        2) echo 0xFFFFFFFE ;;
        3) echo 0xFFFFFFFD ;;
        4) echo 0xFFFFFFFC ;;
        5) echo 0xFFFFFFFB ;;
        6) echo 0xFFFFFFFA ;;
        7) echo 0xFFFFFFF9 ;;
        *) return 1 ;;
    esac
}

# $1 = state (1 press, 0 release), $2 = ex_info
focus_send() {
    if [ -n "$SHUTTER_DRYRUN" ]; then
        echo "[dry-run] $SNDCMD --sync $INFRA_INPUT_TESTCMD d:$INFRA_INPUT_CMD_SEND_EVENT d:$KEY_FOCUS_DRIVE d:0 d:$1 d:$2"
        return 0
    fi
    _out=$("$SNDCMD" --sync $INFRA_INPUT_TESTCMD d:$INFRA_INPUT_CMD_SEND_EVENT d:$KEY_FOCUS_DRIVE d:0 d:$1 d:$2 2>&1)
    _rc=$?
    if [ $_rc -ne 0 ]; then
        echo "focus: sndcmd failed (rc=$_rc) state $1 ex_info $2: $_out" >&2
        return $_rc
    fi
    return 0
}

# One focus move: $1 = near|far, $2 = amount 1..7 (small 1, medium 3, large 7)
focus_push() {
    _ex=$(focus_amount "$1" "$2") || return 2
    if ! focus_send 1 "$_ex"; then
        focus_send 0 "$_ex"
        return 1
    fi
    focus_send 0 "$_ex"
}

focus_release() {
    focus_send 0 0
}

# small|medium|large|1..7 -> 1..7
focus_size() {
    case "$1" in
        small) echo 1 ;;
        medium) echo 3 ;;
        large) echo 7 ;;
        [1-7]) echo "$1" ;;
        *) return 1 ;;
    esac
}
