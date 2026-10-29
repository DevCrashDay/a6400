#!/bin/sh
# probe_shutter.sh - first on-camera test of script-driven shutter control.
#
# Run from the service shell with the camera in shooting mode (not playback,
# not a menu), lens cap on is fine. Watch the camera screen while it runs.
#
#   probe_shutter.sh          checks + half-press test + one picture
#   probe_shutter.sh half     checks + half-press test only (takes no picture)
#
# It does not change any setting and writes nothing outside /tmp.

DIR=${0%/*}
[ "$DIR" = "$0" ] && DIR=.
SHUTTER_LIB=1 . "$DIR/shutter.sh"

step() {
    echo ""
    echo "==== $* ===="
}

# /proc/net/tcp lists ports in hex; state 0A = LISTEN.
port_listening() {
    grep -i ": *[0-9A-F]*:$1 [0-9A-F]*:[0-9A-F]* 0A" /proc/net/tcp > /dev/null 2>&1
}

step "1. Sony test tools present"
for f in /usr/bin/sndcmd.elf /usr/bin/testcmd.elf; do
    if [ -x "$f" ]; then echo "ok      $f"; else echo "MISSING $f"; fi
done
if [ ! -x "$SNDCMD" ]; then
    echo "Cannot continue without $SNDCMD"
    exit 1
fi

step "2. Remote-control servers (fallback paths, informational)"
if port_listening 2710; then
    echo "port 10000 LISTEN: orb-server (Sony Camera Remote API) is up"
else
    echo "port 10000 closed: Camera Remote API not running (normal unless Ctrl w/ Smartphone is active)"
fi
if port_listening 3D7C; then
    echo "port 15740 LISTEN: PTP/IP is up"
else
    echo "port 15740 closed: PTP/IP not running"
fi
sleep 2

step "3. Half-press test (S1 held for 3 s)"
echo "Watch the screen: expect the focus frame / AF beep, or the display waking up."
if shutter_s1_on; then
    echo "S1 press sent OK"
else
    echo "S1 press FAILED (see error above)"
    exit 1
fi
sleep 3
shutter_s1_off && echo "S1 release sent OK"
sleep 2

if [ "$1" = "half" ]; then
    echo ""
    echo "Half-press test done. Did the camera react? Run without 'half' to take a picture."
    exit 0
fi

step "4. Full shot (S1 + S2)"
echo "Expect the shutter sound and a new picture in review."
shutter_shot && echo "shot sequence sent OK"
sleep 3

step "Done"
echo "If the camera reacted to both tests, intervalometer.sh will work."
echo "If nothing happened but every send said OK, the camera ignored remote keys:"
echo "turn on Ctrl w/ Smartphone, let a phone connect once, and run this again."
exit 0
