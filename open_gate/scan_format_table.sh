#!/bin/sh
# scan_format_table.sh - read-only: find where the camera image keeps its
# video format table in physical memory.
#
# Reads ONLY the warm-boot (wbimem=) regions from /proc/cmdline. The kernel
# itself reads these when it saves the warm-boot image, and the main camera
# app maps parts of them, so they are known to be readable. Never reads
# anything else: the first scan (v1) read physical 0x00004000 and the camera
# crashed with an external abort and rebooted.
#
# Reports each 256 KB block containing the pattern (default 3840X2160_24P).
# Progress goes to the screen; takes several minutes on the camera.
# Never writes memory, changes no settings, writes only /tmp/open_gate_scan.txt.
#
#   scan_format_table.sh [PATTERN]

OUT=/tmp/open_gate_scan.txt
PAT=${1:-3840X2160_24P}

# $1 = first 4 KB page, $2 = number of pages, $3 = label
scan() {
    pg=$1
    end=$(($1 + $2))
    echo "== $3: pages $1-$((end - 1))"
    echo "scanning $3 ($2 pages)..." >&2
    while [ $pg -lt $end ]; do
        n=64
        [ $((pg + n)) -gt $end ] && n=$((end - pg))
        if dd if=/dev/mem bs=4096 skip=$pg count=$n 2>/dev/null | grep "$PAT" >/dev/null; then
            echo "hit at page $pg (+$n pages)"
            echo "  hit at page $pg" >&2
        fi
        pg=$((pg + n))
    done
}

{
    echo "pattern: $PAT"
    scan 131112 236 "wbimem 0xEC000@0x20028000"
    scan 65536 2413 "wbimem 0x96D000@0x10000000"
    scan 131584 14614 "wbimem 0x3916000@0x20200000"
    echo "done"
} > "$OUT"

cat "$OUT"
echo ""
echo "Saved to $OUT"
