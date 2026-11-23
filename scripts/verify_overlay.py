#!/usr/bin/env python3
"""Verify that the files used to compile QEMU equal the committed overlay."""
from pathlib import Path
import hashlib,json,sys
root=Path(__file__).resolve().parent.parent
source=Path(sys.argv[1])
files=[]
for item in sorted((root/'qemu-source-overlay').rglob('*')):
    if not item.is_file() or item.name.startswith('._') or item.name=='CHANGES.txt':continue
    rel=item.relative_to(root/'qemu-source-overlay');target=source/rel
    digest=hashlib.sha256(item.read_bytes()).hexdigest()
    actual=hashlib.sha256(target.read_bytes()).hexdigest() if target.exists() else None
    files.append({'file':rel.as_posix(),'expected':digest,'actual':actual,'matches':digest==actual})
result={'passed':all(f['matches'] for f in files),'files':files}
print(json.dumps(result,indent=2))
raise SystemExit(0 if result['passed'] else 1)
