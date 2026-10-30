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

# Uses only applets the camera's BusyBox 1.13 has (no tee, head, awk, wc).

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
    show /proc/meminfo

    section "loaded modules"
    show /proc/modules

    section "device mappings in running processes"
    # Which /dev nodes and anonymous shared regions the camera apps map,
    # and how big they are. Shared camera-core memory would show up here.
    for d in /proc/[0-9]*; do
        [ -r "$d/maps" ] || continue
        m=$(grep -E ' [r-][w-][x-]s | /dev/' "$d/maps" 2>/dev/null)
        [ -n "$m" ] || continue
        echo "-- pid ${d#/proc/}: $(tr '\0' ' ' < "$d/cmdline" 2>/dev/null)"
        echo "$m"
    done
} > "$OUT" 2>&1

cat "$OUT"
echo ""
echo "Saved to $OUT"
