#!/usr/bin/env python3
import argparse
import struct
import subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--runs',type=int,default=100000);a=p.parse_args()
a.work.mkdir(parents=True,exist_ok=True)
floats=[0,0x80000000,0x3f000000,0xbf000000,0x3f800000,0xbf800000,0x7f800000,0xff800000,0x7fc00001,1,0x7f7fffff]
payload=b''.join(struct.pack('<I',word) for word in floats)+struct.pack('<ddd',float('nan'),float('inf'),1e300)
for layout in (*range(6),6,255):
    for frames in (0,1,3,17,257):
        for repeat in (0,1):
            header=struct.pack('<BHBBB',layout,frames,31,15,repeat)
            (a.work/f'layout-{layout}-frames-{frames}-repeat-{repeat}').write_bytes(header+payload)
subprocess.run([str(a.binary.resolve()),str(a.work.resolve()),f'-runs={a.runs}','-max_len=2048',f'-artifact_prefix={a.work.resolve()}/'],check=True)
