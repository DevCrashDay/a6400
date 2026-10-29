#!/bin/sh
# probe_rtos_mem.sh - read-only probe: can Linux see the camera core's memory?
#
# Open-gate video would need runtime changes to the camera image (av-cam.bin),
# which runs outside Linux. This only lists what the kernel exposes. It never
# reads or writes raw memory, changes no settings, and writes only to /tmp.
#
#   probe_rtos_mem.sh            prints the report and saves it to
#                                /tmp/open_gate_probe.txt (pull that file)

OUT=/tmp/open_gate_probe.txt

section() {
    echo ""
    echo "==== $* ===="
}

show() {
    if [ -r "$1" ]; then
        cat "$1"
    else
        echo "(cannot read $1)"
    fi
}

{
    section "kernel"
    uname -a
    show /proc/cmdline

    section "/proc/iomem (physical address map)"
    show /proc/iomem

    section "memory device nodes"
    ls -l /dev/mem /dev/kmem /dev/port 2>&1
    grep -E ' (mem|osal|uipc|lld|ldec|dmac)' /proc/devices 2>&1

    section "/proc/meminfo"
    head -n 8 /proc/meminfo 2>&1

    section "loaded modules"
    show /proc/modules

    section "device mappings in running processes"
    # Which /dev nodes and anonymous shared regions the camera apps map,
    # and how big they are. Shared camera-core memory would show up here.
    for d in /proc/[0-9]*; do
        [ -r "$d/maps" ] || continue
        m=$(awk '$2 ~ /s$/ || $6 ~ /^\/dev\// { print $1, $2, $6 }' "$d/maps" 2>/dev/null | sort -u | head -n 40)
        [ -n "$m" ] || continue
        echo "-- pid ${d#/proc/}: $(tr '\0' ' ' < "$d/cmdline" 2>/dev/null | cut -c1-80)"
        echo "$m"
    done
} 2>&1 | tee "$OUT"

echo ""
echo "Saved to $OUT"
