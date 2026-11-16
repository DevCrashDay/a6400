#!/bin/sh
# dump_ram_to_sd.sh - copy camera RAM regions to the SD card for offline
# analysis on the PC. Read-only for camera memory; writes only to the card.
#
# Reads only ranges that are known to be readable (the warm-boot regions,
# which the kernel itself reads). Never reads physical 0-1 MB: that crashed
# the camera (open_gate scan v1).
#
#   dump_ram_to_sd.sh          dump the warm-boot regions (about 67 MB)
#   dump_ram_to_sd.sh --core   also dump the 14 MB block at 0x80000000.
#                              Untested: if it is protected the camera
#                              reboots by itself, like scan v1 did. The
#                              earlier dumps are already on the card by then.
#
# Card: FAT32 (the camera can't mount exFAT), at least 100 MB free. Don't
# open the camera's USB drive on the PC while this runs. When it finishes,
# power the camera off, take the card out and read it in a card reader.

MNT=/mnt
DIR=$MNT/RAMDUMP

if ! grep " $MNT vfat" /proc/mounts >/dev/null; then
    echo "SD card not mounted, trying /dev/mmca1 ..."
    mount -t vfat /dev/mmca1 $MNT || {
        echo "Could not mount the SD card on $MNT. Is it FAT32?"
        exit 1
    }
fi
mkdir -p $DIR || exit 1
df $MNT

# $1 = name, $2 = first 4 KB page, $3 = page count
dump() {
    echo "dumping $1 ($3 pages)..."
    dd if=/dev/mem of=$DIR/$1.bin bs=4096 skip=$2 count=$3
    sync
    ls -l $DIR/$1.bin
}

dump phys_20028000 131112 236
dump phys_10000000 65536 2413
dump phys_20200000 131584 14614

if [ "$1" = "--core" ]; then
    echo "dumping 0x80000000 (if the camera reboots now, that block is protected)"
    dump phys_80000000 524288 3584
fi

cat /proc/cmdline > $DIR/cmdline.txt
sync
echo ""
echo "Done. Files are in RAMDUMP on the SD card."
echo "Exit the shell and power the camera off before taking the card out."
