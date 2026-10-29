#!/bin/sh
# scan_core_image.sh - read-only: check that /dev/mem reads return data, then
# look for the camera-core image in the 14 MB reserved block at 0x80000000
# (memrsv=14M@0x80000000), the likeliest home of av-cam.bin (about 12 MB).
#
# Step 1 reads one page the main app maps itself (0x20028000), so it is safe.
# Step 2 reads one page at 0x80000000 first. If that address is protected the
# camera crashes and reboots by itself (as scan v1 did at 0x4000); nothing is
# written either way. Only then does it scan the rest of the 14 MB.
#
# Prints progress on screen; writes only /tmp/open_gate_core.txt.
#
#   scan_core_image.sh [PATTERN]

OUT=/tmp/open_gate_core.txt
PAT=${1:-3840X2160_24P}

# $1 = page, prints the dd record counts so we can see whether a read worked
try_page() {
    dd if=/dev/mem bs=4096 skip=$1 count=1 of=/tmp/og_page.bin 2>&1
    ls -l /tmp/og_page.bin 2>&1
    grep -c . /tmp/og_page.bin 2>&1
    rm -f /tmp/og_page.bin
}

# $1 = first 4 KB page, $2 = number of pages
scan() {
    pg=$1
    end=$(($1 + $2))
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
    echo "== step 1: known-good page 0x20028000"
    echo "step 1..." >&2
    try_page 131112
    echo "== step 2: first page of 0x80000000"
    echo "step 2 (if the camera reboots here, 0x80000000 is protected)..." >&2
    try_page 524288
    echo "== step 3: scan 0x80000000-0x80DFFFFF"
    echo "step 3: scanning 14 MB..." >&2
    scan 524288 3584
    echo "done"
} > "$OUT"

cat "$OUT"
echo ""
echo "Saved to $OUT"
