#!/bin/sh
# Sony a6400 In-Camera Film Lab ("The Ritual") & Autoexec Suite

# 0. Give the shell a known environment so external tools resolve even when
#    the hook's execve passed an empty environment (mount/nice/killall/date).
export PATH=/usr/bin:/bin:/sbin:/usr/sbin:/tmp

# 1. Immediately record PID to mark autoexec as running
echo $$ > /tmp/autoexec.pid

# 1a. CRITICAL: remove our boot lock whenever this script exits.
#     /tmp is PERSISTENT vfat flash on this camera (nflasha10), NOT tmpfs, and
#     there is no volatile filesystem anywhere. If the hook's O_EXCL lock at
#     /tmp/autoexec.lock were left behind, the preload hook's lock acquisition
#     would fail on the NEXT boot and homebrew would silently stop launching.
#     Clearing it on exit keeps every future boot clean. The idempotence guards
#     below make it safe for a late-starting process to re-enter this script
#     after the lock is gone (services already up are not started twice).
trap 'rm -f /tmp/autoexec.lock 2>/dev/null' EXIT

# 2. Mount SD card to access photos and configuration
mount -t vfat /dev/mmca1 /mnt 2>/dev/null

# 3. KILL-SWITCH CHECK: SD card or /tmp
#    (This is the authoritative killswitch: the preload hook cannot check the
#    SD card because /mnt is not mounted yet when it runs at early boot.)
if [ -f /mnt/disable_hook ] || [ -f /mnt/disable.txt ] || [ -f /tmp/disable_hook ]; then
    echo "[!] KILL-SWITCH DETECTED! Uninstalling preload hook..." > /tmp/autoexec.log
    rm -f /etc/ld.so.preload /tmp/ld.so.preload /tmp/autoexec.lock 2>/dev/null
    exit 0
fi

echo "[+] The Ritual autoexec started at $(date)" > /tmp/autoexec.log

# 4. Synchronize binaries and permissions
for bin in busybox ritual_daemon ritual_menu arw_develop sony_ptpip star_trails timelapse_mux; do
    if [ -f "/mnt/$bin" ]; then
        cp "/mnt/$bin" "/tmp/$bin" 2>/dev/null
        chmod 755 "/tmp/$bin" "/mnt/$bin" 2>/dev/null
    elif [ -f "/mnt/a6400hb/bin/$bin" ]; then
        cp "/mnt/a6400hb/bin/$bin" "/tmp/$bin" 2>/dev/null
        chmod 755 "/tmp/$bin" "/mnt/a6400hb/bin/$bin" 2>/dev/null
    fi
done

chmod 755 /mnt/*.sh /mnt/a6400hb/*.sh 2>/dev/null
chmod +x /mnt/www/cgi-bin/*.sh 2>/dev/null

# Ensure /mnt/RITUAL.ini exists
if [ ! -f /mnt/RITUAL.ini ]; then
    cat << 'EOF' > /mnt/RITUAL.ini
STOCK=PORTRA_400
GRAIN=MEDIUM
HALATION=ON
EOF
fi

# Ensure /mnt/a6400hb/config.json exists if missing
if [ ! -f /mnt/a6400hb/config.json ] && [ -f /mnt/config.json ]; then
    mkdir -p /mnt/a6400hb 2>/dev/null
    cp /mnt/config.json /mnt/a6400hb/config.json 2>/dev/null
fi

# 5. Start Telnet Daemon (Port 2323) -- OPT-IN ONLY.
#    telnetd -l /bin/sh is an UNAUTHENTICATED root shell to anyone on the
#    camera's Wi-Fi, so it is gated behind an explicit flag file. Create
#    /mnt/enable_telnet on the SD card to turn it on. (A duplicate re-entry
#    simply fails to re-bind the port, which is harmless.)
if [ -f /mnt/enable_telnet ] && [ -x /tmp/busybox ]; then
    /tmp/busybox telnetd -p 2323 -l /bin/sh >> /tmp/autoexec.log 2>&1
    echo "[+] telnetd listening on port 2323 (enabled via /mnt/enable_telnet)" >> /tmp/autoexec.log
else
    echo "[i] telnetd NOT started (create /mnt/enable_telnet to enable)" >> /tmp/autoexec.log
fi

# 6. Start Web Darkroom UI (Port 8080) for phone / Wi-Fi interaction
if [ -d /mnt/www ] && [ -x /tmp/busybox ]; then
    /tmp/busybox httpd -p 8080 -h /mnt/www >> /tmp/autoexec.log 2>&1
    echo "[+] The Ritual Web UI listening on port 8080" >> /tmp/autoexec.log
fi

# 7. Start The Ritual In-Camera Film Engine (lowest priority nice 19).
#    Idempotent: only start if it is not already running, so a re-entry of this
#    script (after the boot lock was cleared) never spawns a second daemon.
if [ -x /tmp/ritual_daemon ] && ! pidof ritual_daemon >/dev/null 2>&1; then
    nice -n 19 /tmp/ritual_daemon >> /tmp/ritual.log 2>&1 &
    echo "[+] The Ritual film engine launched in background!" >> /tmp/autoexec.log
else
    echo "[i] ritual_daemon not started (missing or already running)" >> /tmp/autoexec.log
fi

# 8. Start Runtime Watchdog & Supervisor (SD card health, killswitch, thermal/battery).
#    Idempotent via its PID file so a re-entry does not start a second watchdog.
watchdog_alive() {
    [ -f /tmp/watchdog.pid ] && kill -0 "$(cat /tmp/watchdog.pid 2>/dev/null)" 2>/dev/null
}
if ! watchdog_alive; then
    if [ -f /mnt/watchdog.sh ]; then
        nice -n 19 /bin/sh /mnt/watchdog.sh >> /tmp/watchdog.log 2>&1 &
        echo "[+] Watchdog supervisor active" >> /tmp/autoexec.log
    elif [ -f /mnt/a6400hb/watchdog.sh ]; then
        nice -n 19 /bin/sh /mnt/a6400hb/watchdog.sh >> /tmp/watchdog.log 2>&1 &
        echo "[+] Watchdog supervisor active" >> /tmp/autoexec.log
    fi
else
    echo "[i] watchdog already running" >> /tmp/autoexec.log
fi

# 9. Recording Limit Removal (opt-in via /mnt/enable_rec_unlimit flag file).
#    Writes three Backup.bin properties to extend the max recording time from
#    29m50s to 13h01m00s. Safe, reversible, and well-tested across Sony models.
#    Opt-out / restore: create /mnt/disable_rec_unlimit to revert to factory.
BK=/usr/bin/bk.elf
if [ -f /mnt/disable_rec_unlimit ]; then
    "$BK" w 003c0373 00 2>/dev/null
    "$BK" w 003c0374 1D 2>/dev/null
    "$BK" w 003c0375 32 2>/dev/null
    "$BK" s 2>/dev/null
    echo "[+] Recording limit RESTORED to factory 29m50s (disable_rec_unlimit found)" >> /tmp/autoexec.log
elif [ -f /mnt/enable_rec_unlimit ]; then
    "$BK" w 003c0373 0D 2>/dev/null
    "$BK" w 003c0374 01 2>/dev/null
    "$BK" w 003c0375 00 2>/dev/null
    "$BK" s 2>/dev/null
    echo "[+] Recording limit REMOVED — max ~13h per clip (enable_rec_unlimit found)" >> /tmp/autoexec.log
else
    echo "[i] Recording limit unchanged (no enable/disable flag on SD)" >> /tmp/autoexec.log
fi
