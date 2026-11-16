#!/bin/sh
# dump_block_09915000.sh - copy the reserved block 0x09915000-0x0FFFFFFF
# (about 103 MB, memrsv=0xA6EB000@0x09915000, up to the 0x10000000 region
# already dumped) to RAMDUMP/blk09915000/ on the SD card.
#
# Reads one page first. If the block is protected the camera reboots at that
# point, like 0x4000 and 0x80000000 did; nothing is written to the camera.
# After that it dumps 4 MB files, each synced, so a later reboot keeps
# everything dumped up to then.
#
# Card: FAT32, about 110 MB free. Don't open the camera's USB drive on the PC
# while this runs. When done, power off and copy RAMDUMP to the PC.

MNT=/mnt
DIR=$MNT/RAMDUMP/blk09915000
START=39189
END=65536
CHUNK=1024

if ! grep " $MNT vfat" /proc/mounts >/dev/null; then
    echo "SD card not mounted, trying /dev/mmca1 ..."
    mount -t vfat /dev/mmca1 $MNT || {
        echo "Could not mount the SD card on $MNT. Is it FAT32?"
        exit 1
    }
fi
mkdir -p $DIR || exit 1
df $MNT

echo "reading the first page (the camera reboots here if the block is protected)..."
dd if=/dev/mem of=$DIR/first_page.bin bs=4096 skip=$START count=1
sync
echo "first page OK, dumping the block in 4 MB files..."

pg=$START
while [ $pg -lt $END ]; do
    n=$CHUNK
    [ $((pg + n)) -gt $END ] && n=$((END - pg))
    echo "page $pg ($n pages)"
    dd if=/dev/mem of=$DIR/page_$pg.bin bs=4096 skip=$pg count=$n 2>/dev/null
    sync
    pg=$((pg + n))
done

echo ""
echo "Done. Exit the shell and power the camera off before taking the card out."
