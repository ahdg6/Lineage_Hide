#!/usr/bin/env python3
"""Check extracted ELF load segments and RELRO on 16 KiB Android kernels."""
import subprocess
import sys

if len(sys.argv) < 2:
    raise SystemExit('usage: verify-elf-alignment.py ELF [ELF ...]')

for path in sys.argv[1:]:
    headers = subprocess.check_output(['readelf', '--wide', '--program-headers', path], text=True)
    loads = 0
    for line in headers.splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == 'LOAD':
            loads += 1
            offset, address, alignment = (int(fields[i], 16) for i in [1, 2, -1])
            if alignment < 16384 or (address - offset) % 16384:
                raise SystemExit(f'{path}: LOAD segment is not 16 KiB aligned')
        elif fields[0] == 'GNU_RELRO':
            address, size = (int(fields[i], 16) for i in [2, 5])
            if (address + size) % 16384:
                raise SystemExit(f'{path}: RELRO end is not 16 KiB aligned')
    if not loads:
        raise SystemExit(f'{path}: missing LOAD segments')
    print(f'{path}: 16 KiB LOAD/RELRO checks passed')
