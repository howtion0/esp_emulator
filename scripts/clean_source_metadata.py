#!/usr/bin/env python3
"""Remove only AppleDouble sidecars from an extracted source tree."""
from pathlib import Path
import sys
import stat
count=0
for file in Path(sys.argv[1]).rglob('._*'):
    if file.is_file():
        with file.open('rb') as stream:
            header=stream.read(4)
        if header==b'\x00\x05\x16\x07':
            file.chmod(file.stat().st_mode | stat.S_IWRITE)
            file.unlink()
            count+=1
print(f'Removed {count} AppleDouble metadata sidecars')
