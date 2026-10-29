#!/bin/sh
# Sony a6400 Display (/dev/fb0, DMP) & Button Input (/proc/input) Probe
# Compatible with BusyBox 1.13.4 ash (LF line endings required)

export PATH=/usr/bin:/bin:/sbin:/usr/sbin:/tmp

echo "=== 1. Framebuffer & DMP Device Nodes ==="
ls -l /dev 2>/dev/null | grep -iE 'fb|dmp' || echo "(no fb/dmp nodes in /dev)"

echo ""
echo "=== 2. /proc/fb ==="
if [ -r /proc/fb ]; then
    cat /proc/fb
else
    echo "/proc/fb not readable"
fi

echo ""
echo "=== 3. /proc/input Key Table ==="
if [ -w /proc/input ]; then
    echo table > /proc/input 2>/dev/null
    cat /proc/input 2>/dev/null
    echo get > /proc/input 2>/dev/null
    echo "--- /proc/input (get mode) ---"
    cat /proc/input 2>/dev/null
else
    echo "/proc/input not writable/present"
fi

echo ""
echo "=== 4. a6400_gui.elf Probe ==="
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