#!/bin/sh
# rec_unlimit.sh — Remove the 30-minute video recording limit on Sony a6400
#
# Uses the camera's built-in `bk` tool to modify three Backup.bin properties
# that store the maximum continuous recording time (hours, minutes, seconds).
#
# Backup property IDs (from OpenMemories-Tweak / OpenMemories-Platform):
#   0x003c0373  Recording limit — Hours   (1 byte)
#   0x003c0374  Recording limit — Minutes (1 byte)
#   0x003c0375  Recording limit — Seconds (1 byte)
#
# Factory default: 00 1D 32  → 0h 29m 50s  (just under the EU 30-min threshold)
# Unrestricted:    0D 01 00  → 13h 01m 00s  (near 32-bit 90kHz timer max)
#
# The recording limit value is converted to a 90 KHz clock internally.
# The theoretical overflow boundary is 13h 15m 21s; 13h 01m 00s is the value
# used by Sony's own camcorders that ship without the limit, and by
# OpenMemories-Tweak. It is safe and well-tested across many Sony models.
#
# IMPORTANT: This modifies persistent settings in /setting/Backup.bin.
# The change survives power cycles and reboots. Use --restore to undo.

set -e

BK=/usr/bin/bk.elf
BKID_H=003c0373
BKID_M=003c0374
BKID_S=003c0375

FACTORY_H=00
FACTORY_M=1D
FACTORY_S=32

UNLIMIT_H=0D
UNLIMIT_M=01
UNLIMIT_S=00

LOG=/tmp/rec_unlimit.log

log() {
    echo "[rec_unlimit] $*"
    echo "[rec_unlimit] $(date) $*" >> "$LOG" 2>/dev/null
}

read_current() {
    cur_h=$("$BK" r "$BKID_H" 2>/dev/null | tr -d '[:space:]') || cur_h="??"
    cur_m=$("$BK" r "$BKID_M" 2>/dev/null | tr -d '[:space:]') || cur_m="??"
    cur_s=$("$BK" r "$BKID_S" 2>/dev/null | tr -d '[:space:]') || cur_s="??"
}

show_status() {
    read_current
    h_dec=$((0x$cur_h)) 2>/dev/null || h_dec="?"
    m_dec=$((0x$cur_m)) 2>/dev/null || m_dec="?"
    s_dec=$((0x$cur_s)) 2>/dev/null || s_dec="?"
    log "Current recording limit: ${h_dec}h ${m_dec}m ${s_dec}s (raw: $cur_h $cur_m $cur_s)"

    if [ "$cur_h" = "$UNLIMIT_H" ] && [ "$cur_m" = "$UNLIMIT_M" ] && [ "$cur_s" = "$UNLIMIT_S" ]; then
        log "Status: UNRESTRICTED (13h 01m 00s)"
        return 0
    elif [ "$cur_h" = "$FACTORY_H" ] && [ "$cur_m" = "$FACTORY_M" ] && [ "$cur_s" = "$FACTORY_S" ]; then
        log "Status: FACTORY DEFAULT (0h 29m 50s)"
        return 1
    else
        log "Status: CUSTOM value (not factory, not standard unlimit)"
        return 2
    fi
}

apply_unlimit() {
    read_current
    log "Saving backup of current values: H=$cur_h M=$cur_m S=$cur_s"
    echo "$cur_h $cur_m $cur_s" > /tmp/rec_limit_backup.txt 2>/dev/null

    log "Writing unrestricted values: H=$UNLIMIT_H M=$UNLIMIT_M S=$UNLIMIT_S"
    "$BK" w "$BKID_H" "$UNLIMIT_H"
    "$BK" w "$BKID_M" "$UNLIMIT_M"
    "$BK" w "$BKID_S" "$UNLIMIT_S"

    # Sync to disk
    "$BK" s 2>/dev/null || true

    log "Recording limit removed. The camera can now record up to ~13 hours per clip."
    log "The change takes effect on the next recording. No reboot needed."
}

apply_restore() {
    log "Restoring factory recording limit: H=$FACTORY_H M=$FACTORY_M S=$FACTORY_S"
    "$BK" w "$BKID_H" "$FACTORY_H"
    "$BK" w "$BKID_M" "$FACTORY_M"
    "$BK" w "$BKID_S" "$FACTORY_S"

    "$BK" s 2>/dev/null || true

    log "Factory recording limit restored (0h 29m 50s)."
}

case "${1:-}" in
    --status|-s)
        show_status
        ;;
    --restore|-r)
        apply_restore
        show_status
        ;;
    --help|-h)
        echo "Usage: rec_unlimit.sh [--status|--restore|--help]"
        echo ""
        echo "  (no args)   Remove the 30-min recording limit"
        echo "  --status    Show current recording limit"
        echo "  --restore   Restore factory 29m50s limit"
        echo "  --help      Show this help"
        echo ""
        echo "Killswitch: create /mnt/disable_rec_unlimit on the SD card"
        echo "            to prevent autoexec from applying this at boot."
        ;;
    *)
        apply_unlimit
        show_status
        ;;
esac
