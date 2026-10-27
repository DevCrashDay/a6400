#!/bin/sh
# verify-after-boot.sh — a6400 boot-persistence verifier
# Run this AFTER a real camera power-cycle to confirm the LD_PRELOAD hook
# fired on its own at boot (NOT via a manual LD_PRELOAD command).
# Must be saved with LF (Unix) line endings only!
#
# BEFORE you power-cycle the camera, clear the stale run state so you can
# tell a fresh boot apart from an old manual test:
#   rm -f /tmp/autoexec.log /tmp/autoexec.lock /tmp/autoexec.pid /tmp/ritual.log
# Then turn the camera OFF and ON, re-enter the service shell, and run this.

echo "==== Boot persistence check ===="
echo ""

echo "---- /etc/ld.so.preload (must point at the hook) ----"
if [ -f /etc/ld.so.preload ]; then
    cat /etc/ld.so.preload
else
    echo "FAIL: /etc/ld.so.preload does NOT exist — hook not installed"
fi
echo ""

echo "---- autoexec.log (expect a FRESH boot timestamp) ----"
if [ -f /tmp/autoexec.log ]; then
    cat /tmp/autoexec.log
    echo ""
    echo "(If this log is present and you cleared it before the power-cycle,"
    echo " the hook fired on its own at boot = persistence CONFIRMED.)"
else
    echo "FAIL: no /tmp/autoexec.log — hook did NOT fire at boot"
fi
echo ""

echo "---- ritual.log ----"
if [ -f /tmp/ritual.log ]; then
    cat /tmp/ritual.log
else
    echo "(no ritual.log)"
fi
echo ""

echo "---- Running homebrew processes ----"
FOUND=$(ps | grep -E 'ritual_daemon|telnetd|httpd|watchdog' | grep -v grep | grep -v verify-after-boot)
if [ -n "$FOUND" ]; then
    echo "$FOUND"
else
    echo "(none running)"
fi
echo ""

echo "---- Lock / pid state ----"
ls -la /tmp/autoexec.lock /tmp/autoexec.pid 2>&1
echo ""

echo "==== Done ===="
