#!/usr/bin/env python3
"""
Sony a6400 Backup.bin Analyzer

Parses the camera settings database and outputs a CSV of all settings.
Can also search for specific setting IDs or value patterns.

Usage:
    python analyze_backup_bin.py Backup.bin
    python analyze_backup_bin.py Backup.bin --search-id 0x41a1
    python analyze_backup_bin.py Backup.bin --search-value 1 --single-entry
    python analyze_backup_bin.py Backup.bin --bool-flags
    python analyze_backup_bin.py Backup.bin --csv output.csv
"""

import struct
import sys
import argparse
from collections import defaultdict


def parse_backup_bin(filepath):
    with open(filepath, 'rb') as f:
        data = f.read()

    header = struct.unpack_from('<H', data, 0)[0]

    entries = []
    offset = 2
    while offset + 6 <= len(data):
        v1, v2, sid = struct.unpack_from('<HHH', data, offset)
        entries.append({
            'id': sid,
            'v1': v1,
            'v2': v2,
            'offset': offset,
        })
        offset += 6

    by_id = defaultdict(list)
    for e in entries:
        by_id[e['id']].append(e)

    return header, entries, by_id


def print_summary(header, entries, by_id):
    print(f"Backup.bin Summary")
    print(f"  Header value: {header} (0x{header:04x})")
    print(f"  Total entries: {len(entries)}")
    print(f"  Unique setting IDs: {len(by_id)}")
    print()

    active = sum(1 for sid, el in by_id.items()
                 if any(e['v1'] != 0 or e['v2'] != 0 for e in el))
    print(f"  Active settings (non-zero): {active}")
    print(f"  Zero-only settings: {len(by_id) - active}")


def find_bool_flags(by_id):
    on_flags = []
    off_flags = []
    for sid in sorted(by_id.keys()):
        el = by_id[sid]
        if len(el) == 1 and el[0]['v1'] == 0:
            if el[0]['v2'] == 1:
                on_flags.append(sid)
            elif el[0]['v2'] == 0:
                off_flags.append(sid)
    return on_flags, off_flags


def main():
    parser = argparse.ArgumentParser(description='Sony a6400 Backup.bin Analyzer')
    parser.add_argument('file', help='Path to Backup.bin')
    parser.add_argument('--search-id', help='Search for a specific setting ID (hex, e.g. 0x41a1)')
    parser.add_argument('--search-value', type=int, help='Search for entries with this v2 value')
    parser.add_argument('--single-entry', action='store_true', help='Only show IDs with exactly 1 entry')
    parser.add_argument('--bool-flags', action='store_true', help='Show boolean ON/OFF flag settings')
    parser.add_argument('--csv', help='Export all settings to CSV file')
    parser.add_argument('--raw-dump', help='Hex dump entries for a given ID (hex)')
    args = parser.parse_args()

    header, entries, by_id = parse_backup_bin(args.file)
    print_summary(header, entries, by_id)
    print()

    if args.search_id:
        sid = int(args.search_id, 16) if args.search_id.startswith('0x') else int(args.search_id)
        if sid in by_id:
            el = by_id[sid]
            print(f"Setting 0x{sid:04x} ({sid}): {len(el)} entries")
            for i, e in enumerate(el):
                print(f"  [{i}] v1=0x{e['v1']:04x} ({e['v1']:5d})  v2=0x{e['v2']:04x} ({e['v2']:5d})  offset=0x{e['offset']:06x}")
        else:
            print(f"Setting 0x{sid:04x} not found")

    if args.search_value is not None:
        val = args.search_value
        matches = []
        for sid in sorted(by_id.keys()):
            el = by_id[sid]
            if args.single_entry and len(el) != 1:
                continue
            for e in el:
                if e['v2'] == val:
                    matches.append((sid, e))
        print(f"Entries with v2={val}: {len(matches)}")
        for sid, e in matches[:100]:
            print(f"  0x{sid:04x}: v1={e['v1']:5d} v2={e['v2']:5d} (entries for this ID: {len(by_id[sid])})")
        if len(matches) > 100:
            print(f"  ... and {len(matches) - 100} more")

    if args.bool_flags:
        on_flags, off_flags = find_bool_flags(by_id)
        print(f"Boolean ON flags ({len(on_flags)}):")
        for sid in on_flags:
            print(f"  0x{sid:04x} ({sid})")
        print()
        print(f"Boolean OFF flags ({len(off_flags)}):")
        for sid in off_flags:
            print(f"  0x{sid:04x} ({sid})")

    if args.raw_dump:
        sid = int(args.raw_dump, 16) if args.raw_dump.startswith('0x') else int(args.raw_dump)
        with open(args.file, 'rb') as f:
            data = f.read()
        if sid in by_id:
            for e in by_id[sid]:
                off = e['offset']
                raw = data[off:off+6]
                print(f"  offset 0x{off:06x}: {raw.hex()}")

    if args.csv:
        with open(args.csv, 'w') as f:
            f.write('setting_id_hex,setting_id_dec,entry_count,nonzero_count,values\n')
            for sid in sorted(by_id.keys()):
                el = by_id[sid]
                nonzero = [(e['v1'], e['v2']) for e in el if e['v1'] != 0 or e['v2'] != 0]
                vals = '; '.join(f"({e['v1']},{e['v2']})" for e in el[:20])
                if len(el) > 20:
                    vals += f"... +{len(el)-20} more"
                f.write(f'0x{sid:04x},{sid},{len(el)},{len(nonzero)},"{vals}"\n')
        print(f"CSV written to {args.csv}")


if __name__ == '__main__':
    main()
