#!/bin/sh
# check.sh — a6400 diagnostic script for BusyBox 1.13.4 ash
# Must be saved with LF (Unix) line endings only!

echo "==== 1. Mount / filesystem (confirm /tmp is persistent flash) ===="
grep -E '/tmp|/etc' /proc/mounts
sleep 3

echo ""
echo "==== 2. Hook files present + sizes ===="
ls -la /tmp/autoexec_hook.so 2>&1
ls -la /tmp/autoexec.sh 2>&1
echo "(expect autoexec_hook.so = 45428 bytes)"
sleep 3

echo ""
echo "==== 3. ELF magic (expect: 7f 45 4c 46 ... 28 00 = ARM) ===="
if [ -f /tmp/autoexec_hook.so ]; then
    MAGIC=$(dd if=/tmp/autoexec_hook.so bs=1 count=6 2>/dev/null)
    BYTE1=$(dd if=/tmp/autoexec_hook.so bs=1 skip=0 count=1 2>/dev/null | cat -v)
    BYTE2=$(dd if=/tmp/autoexec_hook.so bs=1 skip=1 count=1 2>/dev/null | cat -v)
    BYTE3=$(dd if=/tmp/autoexec_hook.so bs=1 skip=2 count=1 2>/dev/null | cat -v)
    BYTE4=$(dd if=/tmp/autoexec_hook.so bs=1 skip=3 count=1 2>/dev/null | cat -v)
    echo "First 4 bytes (cat -v): $BYTE1 $BYTE2 $BYTE3 $BYTE4"
    if echo "$BYTE1" | grep -q "\\^\\?"; then
        echo "Byte 0 = 0x7f (DEL) - OK (ELF signature)"
    else
        echo "WARNING: Byte 0 is NOT 0x7f — may not be a valid ELF"
    fi
    if [ "$BYTE2" = "E" ] && [ "$BYTE3" = "L" ] && [ "$BYTE4" = "F" ]; then
        echo "Bytes 1-3 = ELF — valid ELF header confirmed"
    else
        echo "WARNING: Bytes 1-3 not 'ELF' — file may be corrupt"
    fi
else
    echo "autoexec_hook.so NOT FOUND at /tmp/autoexec_hook.so"
fi
sleep 3

echo ""
echo "==== 4. ld.so.preload contents ===="
if [ -f /etc/ld.so.preload ]; then
    cat /etc/ld.so.preload
else
    echo "/etc/ld.so.preload does NOT exist"
fi
sleep 3

echo ""
echo "==== 5. Clear old run state ===="
if [ -f /tmp/autoexec.lock ]; then
    rm -f /tmp/autoexec.lock
    echo "removed /tmp/autoexec.lock"
else
    echo "no /tmp/autoexec.lock found"
fi
if [ -f /tmp/autoexec.pid ]; then
    OLDPID=$(cat /tmp/autoexec.pid)
    if [ -d "/proc/$OLDPID" ]; then
        kill "$OLDPID" 2>/dev/null
        echo "killed old autoexec process (pid $OLDPID)"
    else
        echo "old pid $OLDPID not running"
    fi
    rm -f /tmp/autoexec.pid
    echo "removed /tmp/autoexec.pid"
else
    echo "no /tmp/autoexec.pid found"
fi
HOOKPIDS=$(pidof autoexec.sh 2>/dev/null)
if [ -n "$HOOKPIDS" ]; then
    for P in $HOOKPIDS; do
        kill "$P" 2>/dev/null
    done
    echo "killed running autoexec.sh pids: $HOOKPIDS"
else
    echo "no autoexec.sh processes running"
fi
echo "cleared"
sleep 3

echo ""
echo "==== 6. Fire the hook via LD_PRELOAD (triggers the constructor) ===="
if [ -f /tmp/autoexec_hook.so ]; then
    LD_PRELOAD=/tmp/autoexec_hook.so /bin/true 2>&1
    echo "LD_PRELOAD test fired (exit code: $?)"
else
    echo "SKIP: autoexec_hook.so not found"
fi
sleep 3

echo ""
echo "==== 7. Check if hook launched autoexec.sh ===="
sleep 2
HOOKPIDS=$(pidof autoexec.sh 2>/dev/null)
if [ -n "$HOOKPIDS" ]; then
    echo "SUCCESS: autoexec.sh is running (pids: $HOOKPIDS)"
else
    echo "autoexec.sh not detected — hook may not have fired"
    echo "Check /tmp/autoexec.lock and /tmp/autoexec.log if they exist:"
    ls -la /tmp/autoexec.lock /tmp/autoexec.log 2>&1
fi

echo ""
echo "==== Done ===="
