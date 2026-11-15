#!/usr/bin/env python3
import argparse
import struct
import subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--runs',type=int,default=100000);a=p.parse_args()
a.work.mkdir(parents=True,exist_ok=True)
seeds=[[1,3,0,2,48000,256],[0,3,2,0,48000,256],[2,3,2,2,48000,256]]
for field in range(6):
    for boundary in (0,1,0x7fffffff,0x80000000,0xffffffff):
        fields=seeds[0].copy();fields[field]=boundary;seeds.append(fields)
for i,seed in enumerate(seeds):(a.work/f'seed-{i}').write_bytes(struct.pack('<6I',*seed))
subprocess.run([str(a.binary.resolve()),str(a.work.resolve()),f'-runs={a.runs}','-max_len=512',f'-artifact_prefix={a.work.resolve()}/'],check=True)
