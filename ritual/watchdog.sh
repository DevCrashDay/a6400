#!/bin/sh
# Sony a6400 Homebrew Runtime Harness & Watchdog
# Compatible with built-in BusyBox v1.13.4 (uses pidof, no nice/pgrep/df/awk)

HB_DIR="/mnt/a6400hb"
LOG_DIR="$HB_DIR/logs"
CONFIG="$HB_DIR/config.json"
PID_FILE="/tmp/watchdog.pid"

mkdir -p "$LOG_DIR" 2>/dev/null

log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') [WATCHDOG] $1" >> "$LOG_DIR/system.log" 2>/dev/null
}

# 1. Killswitch check
if [ -f /mnt/disable_hook ] || [ -f /mnt/disable.txt ] || [ -f /tmp/disable_hook ]; then
    log "[KILLSWITCH] Emergency stop requested. Terminating homebrew..."
    killall ritual_daemon a6400_gui.elf wifi_offload 2>/dev/null
    exit 0
fi

echo $$ > "$PID_FILE"
log "Watchdog started with PID $$"

# 2. Main watchdog loop
while true; do
    # Check killswitch
    if [ -f /mnt/disable_hook ] || [ -f /mnt/disable.txt ] || [ -f /tmp/disable_hook ]; then
        log "[KILLSWITCH] Detected during runtime. Exiting."
        killall ritual_daemon a6400_gui.elf wifi_offload 2>/dev/null
        rm -f "$PID_FILE"
        exit 0
    fi

    # Supervise ritual_daemon (always keep running unless disabled in config.json)
    SHOULD_RUN_RITUAL=1
    if [ -f "$CONFIG" ] && grep -q '"film_lab": *false' "$CONFIG" 2>/dev/null; then
        SHOULD_RUN_RITUAL=0
    fi

    if [ "$SHOULD_RUN_RITUAL" = "1" ]; then
        if ! pidof ritual_daemon >/dev/null 2>&1; then
            if [ -x /tmp/ritual_daemon ]; then
                log "Restarting /tmp/ritual_daemon..."
                /tmp/ritual_daemon >> /tmp/ritual.log 2>&1 &
            elif [ -x /mnt/ritual_daemon ]; then
                log "Restarting /mnt/ritual_daemon..."
                /mnt/ritual_daemon >> /tmp/ritual.log 2>&1 &
            fi
        fi
    fi

    # Check if wifi_offload should be running
    if [ -f "$CONFIG" ] && grep -q '"wifi_offload": *true' "$CONFIG" 2>/dev/null; then
        if ! pidof wifi_offload >/dev/null 2>&1; then
            if [ -x /mnt/a6400hb/bin/wifi_offload ]; then
                log "Starting wifi_offload service..."
                /mnt/a6400hb/bin/wifi_offload >> "$LOG_DIR/offload.log" 2>&1 &
            fi
        fi
    fi

    sleep 10
done
