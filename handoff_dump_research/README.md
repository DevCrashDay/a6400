# a6400 Storage Dump & Backup.bin Research Handoff

**Date:** 2026-10-26
**Camera:** Sony ILCE-6400, Firmware v2.00
**SoC:** CXD90045-FPSU (ARMv7, 4-core)

---

## What We Have

### Confirmed Working Access
- **Service shell:** `pmca-console-v0.18-win.exe serviceshell` → BusyBox 1.13.4 root shell
- **USB mode:** Must be set to Mass Storage before connecting
- **Native code execution:** Static ARMv7 ELFs run from /tmp
- **Settings read/write:** `readBackup(id)` / `writeBackup(id)` API via PMCA service shell

### Files Extracted
- `Backup.bin` (1,148,036 bytes) — full camera settings database from /setting partition
- `nflasha2_setting.raw` (20 MB) — raw dump of /setting (FAT12) partition

### Partition Map (16 non-zero partitions, ~1.2 GB total)

| Partition | Mount | Type | Size | Description |
|-----------|-------|------|------|-------------|
| nflasha1 | (none) | vfat | 8 MB | Updater |
| nflasha2 | /setting | vfat | 20 MB | Settings (Backup.bin) ✅ DUMPED |
| nflasha3 | /system | vfat | 48 MB | System main |
| nflasha4 | (none) | raw | 12 MB | MPR Flash Backup |
| nflasha5 | /wbi | wbi | 60 MB | Warm Boot Image |
| nflasha6 | (none) | vfat | 16 MB | Jiritsu |
| nflasha7 | / | ext2 | 8 MB | Rootfs |
| nflasha10 | /tmp | vfat | 12 MB | Work (persistent, writable) |
| nflasha11 | /log | vfat | 500 MB | Log |
| nflasha12 | /cert | vfat | 40 MB | Network/certs |
| nflasha13 | (none) | vfat | 4 MB | UpdaterWork |
| nflasha15 | /usr | ext2 | 300 MB | User binaries |
| nflasha16 | /android/system | ext2 | 140 MB | Android (vestigial) |
| nflasha17 | /android/data | vfat | 100 MB | Android (vestigial) |
| nflasha18 | /lens | vfat | 20 MB | Lens data |
| nflasha23 | /PMBP | prfile | 2 MB | PMBP info/licenses |

---

## Backup.bin Format Analysis

### Structure

```
Offset 0x0000: uint16_le header = 16314 (0x3FBA)
Offset 0x0002: Entry[0]  (6 bytes)
Offset 0x0008: Entry[1]  (6 bytes)
...
Offset 0x118480: Entry[191338] (6 bytes)
```

**Total: 2-byte header + 191,339 six-byte entries = 1,148,036 bytes (exact)**

### Entry Format (6 bytes each)

```
Byte 0-1: uint16_le  value1   (usually 0x0000)
Byte 2-3: uint16_le  value2   (primary value)
Byte 4-5: uint16_le  setting_id
```

- `value2` carries the actual setting value in most entries
- `value1` is non-zero in some entries (possibly extended value or flags)
- Setting IDs repeat — each ID can have multiple entries (array-like settings)

### Statistics

| Metric | Value |
|--------|-------|
| Total entries | 191,339 |
| Unique setting IDs | 17,433 |
| Header value | 16,314 (purpose unclear — not equal to ID or entry count) |
| Active settings (any non-zero value) | 17,350 |
| Zero-only settings | 83 |
| Boolean ON flags (single entry, value=1) | 6 |
| Boolean OFF flags (single entry, value=0) | 76 |
| Setting ID range | 0x0000 – 0xFFFF |

### Boolean ON Flags (potential feature enables)

These 6 settings have exactly 1 entry with value = 1:

| Setting ID | Hex |
|-----------|-----|
| 0x41a1 | Currently ON |
| 0x4bb9 | Currently ON |
| 0x4bba | Currently ON |
| 0x68d6 | Currently ON |
| 0x6e5a | Currently ON |
| 0x9b92 | Currently ON |

### Boolean OFF Flags (76 settings — potential hidden features)

76 settings have exactly 1 entry with value = 0. These are candidates for
"flip to 1 to enable" experimentation. See `backup_bin_settings.csv` for the
full list.

### On-Disk vs Runtime IDs

The on-disk Backup.bin uses **2-byte setting IDs** (e.g. 0x4052, 0x419F).
The camera's runtime API (`readBackup()` / `writeBackup()`) uses **4-byte IDs**
(e.g. 0x010d008f for language base). The mapping between them is not yet known.

A community gist for language unlocking uses:
- Base ID: `0x010d008f` (35 language slots, values 1=enabled, 2=disabled)
- `setBackupProtection(False)` before writes, `setBackupProtection(True)` after
- `syncBackup()` to persist changes to flash

---

## Dump Transfer Workflow

### PMCA `pull` Bug
Every file transferred via PMCA `pull` loses exactly **32,768 bytes** (8 blocks)
from the end. A 917,504-byte file arrives as 884,736 bytes.

### Chunking Workaround (used for nflasha2)

```sh
# On camera (BusyBox shell):
i=0; n=0; while [ $i -lt 5120 ]; do \
  dd if=/dev/nflasha2 of=/log/p2_$n.raw bs=4096 count=224 skip=$i 2>/dev/null; \
  i=$((i+216)); n=$((n+1)); \
done
```

Then Ctrl+C to PMCA prompt, `pull /log/p2_0.raw p2_0.raw` for each chunk, re-enter `shell`, `rm` chunks.

```powershell
# On PC (PowerShell) — reassemble:
$out = [System.IO.File]::Create("nflasha2_setting.raw")
0..23 | ForEach-Object {
    $data = [System.IO.File]::ReadAllBytes("p2_$_.raw")
    $out.Write($data, 0, $data.Length)
}
$out.SetLength(20971520)
$out.Close()
```

### Better Approach: FAT32 SD Card
The camera's SD slot appears as `/dev/mmca10p1` but the exFAT card can't be mounted
(Linux 3.0.27 has no exFAT driver). Format the SD card as FAT32 on the PC first, then:

```sh
mknod /tmp/sd b 250 11
mkdir /tmp/sdmount
mount -t vfat /tmp/sd /tmp/sdmount
dd if=/dev/nflashaN of=/tmp/sdmount/partN.raw bs=4096
```

This bypasses the pull truncation bug entirely and is much faster for large partitions.

---

## Key Community Findings

1. **No OpenMemories-Tweak on a6400** — returns "No Tweaks found" (Sony-PMCA-RE issue #381)
2. **Telnet/ADB marked unavailable** on a6400 by community research (docodethatmatters.com)
3. **BACKUP_PDT_READ crash** — Sony-PMCA-RE issue #733 documents a crash when bulk-reading
   backup properties on the ILCE-6400; per-property `readBackup()`/`writeBackup()` works
4. **Language unlock works** — confirmed via service shell + backup property writes
5. **30-minute record limit removal** — reported working via same service-mode method
6. **libJiritsu.so present** — Jiritsu injection (used on ZV-E10) may be applicable
7. **Writable /etc** — on same vfat partition as /tmp (nflasha10), potential for ld.so.preload

---

## Next Steps

### Priority 1: Map Backup.bin Settings
- [ ] Correlate on-disk 2-byte IDs with runtime 4-byte API IDs
- [ ] Use `readBackup(id)` from service shell to probe known IDs
- [ ] Identify telnet/ADB/debug enable flags (if they exist on this model)
- [ ] Test flipping the 76 boolean OFF flags one at a time

### Priority 2: Dump Remaining Partitions
- [ ] Format SD card as FAT32
- [ ] Dump all 15 remaining partitions via SD card
- [ ] Focus on: nflasha7 (rootfs), nflasha6 (Jiritsu), nflasha15 (/usr), nflasha10 (/tmp+/etc)

### Priority 3: Code Injection Research
- [ ] Test if /etc/ld.so.preload works
- [ ] Explore Jiritsu partition for injection hooks
- [ ] Map running processes that load from writable paths
- [ ] Check for autoexec.sh or boot-time script execution

---

## References

- [OpenMemories-Tweak](https://github.com/ma1co/OpenMemories-Tweak)
- [Sony-PMCA-RE](https://github.com/ma1co/Sony-PMCA-RE)
- [Language unlock gist](https://gist.github.com/gibilli111/eed521cfa7c0be8be8032225da890daa)
- [a6400 research roundup](https://docodethatmatters.com/sony-a6000-a6400-research/)
- Sony-PMCA-RE issues: #381 (no tweaks), #513 (service mode), #733 (BACKUP_PDT_READ crash)
