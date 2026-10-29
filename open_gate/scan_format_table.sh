#!/bin/sh
# scan_format_table.sh - read-only: find where the camera image keeps its
# video format table in physical memory.
#
# Reads RAM through /dev/mem 1 MB at a time and reports each MB that contains
# the pattern (default 3840X2160_24P). Never writes memory, changes no
# settings, and writes only /tmp/open_gate_scan.txt. Takes a few minutes.
# Only RAM ranges are read, never the 0xF... register area.
#
#   scan_format_table.sh [PATTERN]

OUT=/tmp/open_gate_scan.txt
PAT=${1:-3840X2160_24P}

# $1 = first MB, $2 = MB to stop before
scan() {
    mb=$1
    while [ $mb -lt $2 ]; do
        if dd if=/dev/mem bs=1048576 skip=$mb count=1 2>/dev/null | grep "$PAT" >/dev/null; then
            echo "hit MB $mb"
        fi
        mb=$((mb + 1))
    done
    echo "done up to MB $2"
}

{
    echo "pattern: $PAT"
    echo "main app (pid 152):"
    ls -l /proc/152/exe 2>&1
    echo ""
    echo "== RAM 0x00000000-0x3FFFFFFF (MB 0-1023)"
    scan 0 1024
    echo ""
    echo "== RAM 0x80000000-0x8FFFFFFF (MB 2048-2303)"
    dd if=/dev/mem bs=1048576 skip=2048 count=1 2>&1 >/dev/null
    scan 2048 2304
} > "$OUT" 2>&1

cat "$OUT"
echo ""
echo "Saved to $OUT"
