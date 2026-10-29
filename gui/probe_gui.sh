#!/bin/sh
# Sony a6400 Display (/proc/udm/ma_memspace_info, /proc/udm/dmm_extents, /dev/mem) & Button Input (/proc/udm/input) Probe
# Compatible with BusyBox 1.13.4 ash (LF line endings required)

export PATH=/usr/bin:/bin:/sbin:/usr/sbin:/tmp

echo "=== 1. /proc/udm Entries ==="
ls -l /proc/udm 2>/dev/null || echo "(/proc/udm not present)"

echo ""
echo "=== 2. GUI Memory Spaces (/proc/udm/ma_memspace_info) ==="
if [ -r /proc/udm/ma_memspace_info ]; then
    cat /proc/udm/ma_memspace_info 2>/dev/null
else
    echo "/proc/udm/ma_memspace_info not readable"
fi

echo ""
echo "=== 3. DMM Memory Info (/proc/udm/dmm_info & dmm_extents) ==="
if [ -r /proc/udm/dmm_info ]; then
    cat /proc/udm/dmm_info 2>/dev/null
fi
if [ -r /proc/udm/dmm_extents ]; then
    cat /proc/udm/dmm_extents 2>/dev/null
fi

echo ""
echo "=== 4. Button Input (/proc/udm/input) ==="
IN_PROC="/proc/udm/input"
if [ ! -e "$IN_PROC" ]; then
    IN_PROC="/proc/input"
fi
if [ -w "$IN_PROC" ]; then
    echo table > "$IN_PROC" 2>/dev/null
    cat "$IN_PROC" 2>/dev/null
    echo get > "$IN_PROC" 2>/dev/null
    echo "--- $IN_PROC (get mode) ---"
    cat "$IN_PROC" 2>/dev/null
else
    echo "$IN_PROC not writable/present"
fi

echo ""
echo "=== 5. a6400_gui.elf Probe ==="
GUI_BIN=""
for p in /tmp/a6400_gui.elf /mnt/a6400_gui.elf /mnt/gui/a6400_gui.elf ./a6400_gui.elf; do
    if [ -x "$p" ]; then
        GUI_BIN="$p"
        break
    fi
done

if [ -n "$GUI_BIN" ]; then
    "$GUI_BIN" info
    if [ "$1" = "--banner" ]; then
        echo "Drawing 3-second test banner..."
        "$GUI_BIN" banner "a6400 Homebrew GUI OK" 3
    fi
else
    echo "a6400_gui.elf not found in /tmp or /mnt"
fi
