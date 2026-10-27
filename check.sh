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
echo "==== 3. Validate autoexec_hook.so ===="
if [ -f /tmp/autoexec_hook.so ]; then
    set -- $(ls -l /tmp/autoexec_hook.so)
    FSIZE=$5
    echo "File size: $FSIZE bytes (expect 45428)"
    if [ "$FSIZE" = "45428" ]; then
        echo "Size OK"
    else
        echo "WARNING: size mismatch — file may be truncated or corrupt"
    fi
    ELF_STR=$(dd if=/tmp/autoexec_hook.so bs=1 skip=1 count=3 2>/dev/null)
    if [ "$ELF_STR" = "ELF" ]; then
        echo "ELF signature: OK (bytes 1-3 = ELF)"
    else
        echo "WARNING: bytes 1-3 are not 'ELF' — may not be a valid ELF"
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
# Kill stale homebrew daemons so each run starts clean (no "Address in use",
# no "already running"). /tmp is persistent flash and there's no reboot
# between test runs, so prior instances survive.
for PROC in ritual_daemon watchdog.sh httpd telnetd; do
    PIDS=$(pidof "$PROC" 2>/dev/null)
    if [ -n "$PIDS" ]; then
        for P in $PIDS; do
            kill "$P" 2>/dev/null
        done
        echo "killed stale $PROC: $PIDS"
    fi
done
# Clear stale logs and watchdog pid so this run's output is fresh
rm -f /tmp/ritual.log /tmp/watchdog.log /tmp/watchdog.pid
echo "cleared logs and stale daemons"
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
elif [ -f /tmp/autoexec.log ] && [ -s /tmp/autoexec.log ]; then
    echo "SUCCESS: autoexec.log exists and is non-empty — hook fired"
    echo "(autoexec.sh likely finished or is running under a different name)"
elif [ -f /tmp/autoexec.lock ]; then
    echo "LIKELY SUCCESS: autoexec.lock exists — hook started"
    echo "(autoexec.sh may have finished quickly)"
else
    echo "FAIL: no lock, no log, no process — hook did not fire"
fi
ls -la /tmp/autoexec.lock /tmp/autoexec.log 2>&1

echo ""
echo "==== 8. autoexec.log contents ===="
if [ -f /tmp/autoexec.log ]; then
    cat /tmp/autoexec.log
else
    echo "(no autoexec.log)"
fi

echo ""
echo "==== 9. ritual.log contents ===="
if [ -f /tmp/ritual.log ]; then
    cat /tmp/ritual.log
else
    echo "(no ritual.log)"
fi

echo ""
echo "==== Done ===="
