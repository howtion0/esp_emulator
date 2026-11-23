#!/usr/bin/env python3
"""Copy QEMU and its MinGW DLL dependency closure; do not copy Windows DLLs."""
import re,shutil,subprocess,sys
from pathlib import Path
binary,mingw,dest=map(Path,sys.argv[1:4]);dest.mkdir(parents=True,exist_ok=True)
queue=[binary];seen=set()
while queue:
    file=queue.pop()
    if file.name.lower() in seen: continue
    seen.add(file.name.lower());shutil.copy2(file,dest/file.name)
    report=subprocess.check_output([str(mingw/'objdump.exe'),'-p',str(file)],text=True,encoding='utf-8',errors='replace')
    for name in re.findall(r'DLL Name:\s*(\S+)',report):
        dependency=mingw/name
        if dependency.exists():queue.append(dependency)
print('Bundled',len(seen),'runtime files')
