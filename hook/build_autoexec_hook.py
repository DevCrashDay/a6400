#!/usr/bin/env python3
"""
build_autoexec_hook_v2.py

Robust rebuild of the a6400 ld.so.preload boot hook.

Improvements over build_autoexec_hook.py:
  #2  Dynamic-section terminator is handled correctly: DT_INIT is written over
      the existing DT_NULL slot AND a fresh DT_NULL is written in the next slot,
      so the array is always terminated (the old script relied on the bytes
      after the terminator happening to be zero).
  #4  The launched shell gets a real environment (PATH=...), so autoexec.sh's
      external commands (mount / nice / killall / date) resolve regardless of
      the camera's default shell PATH.
  #6  The grandchild calls setsid() before execve, so the launched shell is
      fully detached from the host process's session (no SIGHUP if that process
      exits).

Unchanged by design:
  - patch_addr stays at the start of .text (0x988). On the a6400 this is the
    exported AKSC_InitDecomp8975 compass routine; the a6400 body has no AKM
    magnetometer, so this driver is effectively dead code here and nothing
    calls it at runtime -- which is almost certainly why this .so was chosen
    as the preload host. Clobbering it is low risk on THIS model.
  - LOCK_PATH is /tmp/autoexec.lock. If the on-camera /proc/mounts shows /tmp
    is persistent flash (not tmpfs), change LOCK_PATH to a volatile path and
    rebuild, otherwise a stale lock will disable the hook on the next boot.
"""

import struct
import keystone

TEMPLATE = "libAK8975.so"
OUTPUT = "autoexec_hook.so"

# --- Tunables -------------------------------------------------------------
PATCH_ADDR = 0x988        # start of .text (verified against section header below)
LOCK_PATH = "/tmp/autoexec.lock"
AUTOEXEC_SH = "/tmp/autoexec.sh"
ENV_PATH = "PATH=/usr/bin:/bin:/sbin:/usr/sbin:/tmp"
# -------------------------------------------------------------------------

asm_code = f"""
    push {{r4, r5, r6, r7, lr}}
    sub sp, sp, #64

    // --- Killswitch checks: /mnt/disable_hook, /mnt/disable.txt, /tmp/disable_hook ---
    adr r0, str_mnt_disable_hook
    mov r1, #0
    mov r7, #33               // sys_access
    svc #0
    cmp r0, #0
    beq exit_clean            // exists -> abort

    adr r0, str_mnt_disable_txt
    mov r1, #0
    mov r7, #33
    svc #0
    cmp r0, #0
    beq exit_clean

    adr r0, str_tmp_disable_hook
    mov r1, #0
    mov r7, #33
    svc #0
    cmp r0, #0
    beq exit_clean

    // --- Require /tmp/autoexec.sh to exist ---
    adr r0, str_autoexec_sh
    mov r1, #0
    mov r7, #33
    svc #0
    cmp r0, #0
    bne exit_clean            // missing -> abort

    // --- Atomic per-run lock: open(LOCK, O_WRONLY|O_CREAT|O_EXCL, 0644) ---
    adr r0, str_autoexec_lock
    mov r1, #193              // O_WRONLY|O_CREAT|O_EXCL
    mov r2, #420              // 0644
    mov r7, #5                // sys_open
    svc #0
    cmp r0, #0
    blt exit_clean            // <0 -> another process already launched; abort
    mov r7, #6                // sys_close
    svc #0

    // --- Fork; parent returns to ld.so cleanly ---
    mov r7, #2                // sys_fork
    svc #0
    cmp r0, #0
    bne exit_clean            // parent (or fork error) exits to caller

    // --- Double-fork to fully detach ---
    mov r7, #2
    svc #0
    cmp r0, #0
    beq in_grandchild
    mov r0, #0                // immediate child exits
    mov r7, #1                // sys_exit
    svc #0

in_grandchild:
    // --- setsid() so we leave the host process's session ---
    mov r7, #66               // sys_setsid
    svc #0

    // --- argv = ["/bin/sh", AUTOEXEC_SH, NULL] at sp[0..8] ---
    adr r0, str_bin_sh
    adr r1, str_autoexec_sh
    mov r2, #0
    str r0, [sp, #0]
    str r1, [sp, #4]
    str r2, [sp, #8]

    // --- envp = [ENV_PATH, NULL] at sp[16..20] ---
    adr r3, str_env_path
    str r3, [sp, #16]
    str r2, [sp, #20]

    // --- execve("/bin/sh", argv=sp, envp=sp+16) ---
    adr r0, str_bin_sh
    mov r1, sp
    add r2, sp, #16
    mov r7, #11               // sys_execve
    svc #0

    // execve failed -> exit
    mov r0, #1
    mov r7, #1
    svc #0

exit_clean:
    add sp, sp, #64
    pop {{r4, r5, r6, r7, pc}}

str_mnt_disable_hook:
    .asciz "/mnt/disable_hook"
    .align 2
str_mnt_disable_txt:
    .asciz "/mnt/disable.txt"
    .align 2
str_tmp_disable_hook:
    .asciz "/tmp/disable_hook"
    .align 2
str_autoexec_sh:
    .asciz "{AUTOEXEC_SH}"
    .align 2
str_autoexec_lock:
    .asciz "{LOCK_PATH}"
    .align 2
str_bin_sh:
    .asciz "/bin/sh"
    .align 2
str_env_path:
    .asciz "{ENV_PATH}"
    .align 2
"""


def u32(b, off):
    return struct.unpack_from("<I", b, off)[0]


def main():
    with open(TEMPLATE, "rb") as f:
        so = bytearray(f.read())

    # --- Locate .text and .dynamic from the section headers (no hardcoding) ---
    e_shoff = u32(so, 0x20)
    e_shentsize = struct.unpack_from("<H", so, 0x2E)[0]
    e_shnum = struct.unpack_from("<H", so, 0x30)[0]
    e_shstrndx = struct.unpack_from("<H", so, 0x32)[0]

    shstr_off = u32(so, e_shoff + e_shstrndx * e_shentsize + 0x10)

    text_off = dyn_off = dyn_size = None
    for i in range(e_shnum):
        sh = e_shoff + i * e_shentsize
        name_off = u32(so, sh + 0x00)
        name_end = so.index(b"\x00", shstr_off + name_off)
        name = so[shstr_off + name_off:name_end].decode()
        sh_type = u32(so, sh + 0x04)
        sh_offset = u32(so, sh + 0x10)
        sh_size = u32(so, sh + 0x14)
        if name == ".text":
            text_off = sh_offset
        if sh_type == 6:  # SHT_DYNAMIC
            dyn_off, dyn_size = sh_offset, sh_size

    assert text_off == PATCH_ADDR, f".text at {text_off:#x}, expected {PATCH_ADDR:#x}"
    assert dyn_off is not None, "no .dynamic section"

    # --- Assemble the constructor ---
    ks = keystone.Ks(keystone.KS_ARCH_ARM, keystone.KS_MODE_ARM)
    encoding, _ = ks.asm(asm_code, addr=PATCH_ADDR)
    code = bytes(encoding)

    rodata_off = None
    for i in range(e_shnum):
        sh = e_shoff + i * e_shentsize
        name_off = u32(so, sh + 0x00)
        name_end = so.index(b"\x00", shstr_off + name_off)
        if so[shstr_off + name_off:name_end] == b".rodata":
            rodata_off = u32(so, sh + 0x10)
    assert PATCH_ADDR + len(code) < rodata_off, "constructor would overrun into .rodata"

    so[PATCH_ADDR:PATCH_ADDR + len(code)] = code

    # --- Find the existing DT_NULL terminator in .dynamic ---
    null_off = None
    off = dyn_off
    while off < dyn_off + dyn_size:
        tag = u32(so, off)
        if tag == 0:  # DT_NULL
            null_off = off
            break
        off += 8
    assert null_off is not None, "no DT_NULL in .dynamic"

    # The slot after the terminator must be free (inside .dynamic, currently zero)
    assert null_off + 16 <= dyn_off + dyn_size, "no room to extend .dynamic terminator"
    assert so[null_off + 8:null_off + 16] == b"\x00" * 8, "slot after DT_NULL is not free"

    # DT_INIT (tag 12) over the old terminator, fresh DT_NULL right after.
    struct.pack_into("<II", so, null_off, 12, PATCH_ADDR)
    struct.pack_into("<II", so, null_off + 8, 0, 0)

    with open(OUTPUT, "wb") as f:
        f.write(so)

    print(f"[+] Constructor: {len(code)} bytes at {PATCH_ADDR:#x}")
    print(f"[+] DT_INIT({PATCH_ADDR:#x}) written at {null_off:#x}, DT_NULL at {null_off + 8:#x}")
    print(f"[+] LOCK_PATH = {LOCK_PATH}")
    print(f"[+] Wrote {OUTPUT} ({len(so)} bytes)")


if __name__ == "__main__":
    main()
