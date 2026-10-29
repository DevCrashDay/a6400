#!/bin/sh
# shutter.sh - fire the a6400 shutter from a shell script.
#
# How it works: the camera's input service (InfraInput, inside the main camera
# app) has a factory test hook on OSAL queue 0x007A018C. Test command 2 injects
# a key event exactly like a real button. We press the same virtual S1
# (half-press) and S2 (full-press) keys that Sony's own Wi-Fi remote uses
# (key source 0x1A, codes 0x1A00000D / 0x1A00000E). Sony's tool sndcmd.elf
# delivers the message. See research/SHUTTER_TRIGGER_RESEARCH.md.
#
# Usage:
#   shutter.sh shot             take one picture at the camera's shutter speed
#   shutter.sh bulb SECONDS     hold the shutter SECONDS long (dial set to BULB)
#   shutter.sh half [SECONDS]   half-press for SECONDS (default 2): AF / wake up
#   shutter.sh release          release both keys (use if anything got stuck)
#   shutter.sh s1on|s1off|s2on|s2off   send one raw key event
#
# Environment:
#   SNDCMD=/path/to/sndcmd.elf   override the sender (default /usr/bin/sndcmd.elf)
#   SHUTTER_DRYRUN=1             print the key events instead of sending them
#
# Other scripts can load the functions without running anything:
#   SHUTTER_LIB=1 . /tmp/shutter.sh
#
# BusyBox 1.13.4 on the camera has no printf/getopts/head/awk. Keep LF endings.

SNDCMD=${SNDCMD:-/usr/bin/sndcmd.elf}
INFRA_INPUT_TESTCMD=0x007A018C
INFRA_INPUT_CMD_SEND_EVENT=2
KEY_S1=0x1A00000D
KEY_S2=0x1A00000E

# $1 = key code, $2 = 1 (press) or 0 (release)
shutter_key() {
    if [ -n "$SHUTTER_DRYRUN" ]; then
        echo "[dry-run] $SNDCMD --sync $INFRA_INPUT_TESTCMD d:$INFRA_INPUT_CMD_SEND_EVENT d:$1 d:0 d:$2 d:0"
        return 0
    fi
    _out=$("$SNDCMD" --sync $INFRA_INPUT_TESTCMD d:$INFRA_INPUT_CMD_SEND_EVENT d:$1 d:0 d:$2 d:0 2>&1)
    _rc=$?
    if [ $_rc -ne 0 ]; then
        echo "shutter: sndcmd failed (rc=$_rc) for key $1 state $2: $_out" >&2
        return $_rc
    fi
    return 0
}

shutter_s1_on()  { shutter_key $KEY_S1 1; }
shutter_s1_off() { shutter_key $KEY_S1 0; }
shutter_s2_on()  { shutter_key $KEY_S2 1; }
shutter_s2_off() { shutter_key $KEY_S2 0; }

# Always release in this order so the camera never sees S2 without S1.
shutter_release() {
    shutter_s2_off
    shutter_s1_off
}

# Same sequence as the Wi-Fi remote's RcInputShutter().
shutter_shot() {
    shutter_s1_on || return $?
    if ! shutter_s2_on; then
        shutter_s1_off
        return 1
    fi
    shutter_s2_off
    shutter_s1_off
}

# Same sequence as RcInputStartBulbShooting() / RcInputStopBulbShooting().
# Only exposes for SECONDS when the shutter speed is set to BULB in M mode;
# at any other shutter speed this is just a normal shot.
shutter_bulb() {
    _secs=$1
    shutter_s1_on || return $?
    if ! shutter_s2_on; then
        shutter_s1_off
        return 1
    fi
    sleep "$_secs"
    shutter_s2_off
    shutter_s1_off
}

shutter_half() {
    shutter_s1_on || return $?
    sleep "${1:-2}"
    shutter_s1_off
}

shutter_is_uint() {
    case "$1" in
        ''|*[!0-9]*) return 1 ;;
    esac
    return 0
}

shutter_usage() {
    echo "usage: ${0##*/} shot | bulb SECONDS | half [SECONDS] | release | s1on | s1off | s2on | s2off"
}

shutter_main() {
    if [ ! -x "$SNDCMD" ] && [ -z "$SHUTTER_DRYRUN" ]; then
        echo "shutter: $SNDCMD not found or not executable" >&2
        exit 2
    fi
    case "$1" in
        shot)
            shutter_shot
            ;;
        bulb)
            if ! shutter_is_uint "$2" || [ "$2" -lt 1 ]; then
                echo "shutter: bulb needs a whole number of seconds" >&2
                exit 2
            fi
            # A held S2 keeps the exposure running, so let go on every exit path.
            trap 'shutter_release; exit 130' INT TERM
            trap '' HUP
            shutter_bulb "$2"
            _rc=$?
            trap - INT TERM
            return $_rc
            ;;
        half)
            if [ -n "$2" ] && ! shutter_is_uint "$2"; then
                echo "shutter: half takes whole seconds" >&2
                exit 2
            fi
            trap 'shutter_s1_off; exit 130' INT TERM
            shutter_half "$2"
            ;;
        release) shutter_release ;;
        s1on)    shutter_s1_on ;;
        s1off)   shutter_s1_off ;;
        s2on)    shutter_s2_on ;;
        s2off)   shutter_s2_off ;;
        *)
            shutter_usage >&2
            exit 2
            ;;
    esac
}

if [ -z "$SHUTTER_LIB" ]; then
    shutter_main "$@"
    exit $?
fi
