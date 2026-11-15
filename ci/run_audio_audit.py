#!/usr/bin/env python3
"""Record observations on private virtual endpoints; impose no hardware latency SLA."""
import argparse
import json
import platform
import subprocess
from pathlib import Path

p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--seconds',type=int,default=60);args=p.parse_args()
if not 1<=args.seconds<=86400:p.error('--seconds must be in [1,86400]')
args.output.mkdir(parents=True,exist_ok=True)
settings={'alsa':('null',512),'jack':('system',256),'pipewire':('mka_test_sink',1024),'pulseaudio':('mka_test_sink',1024)}
cache=(args.build/'CMakeCache.txt').read_text();results=[]
for name,(device,frames) in settings.items():
    if f'MKA_BUILD_{name.upper()}:BOOL=ON' not in cache:continue
    path=args.output/f'{name}.json'
    subprocess.run([str((args.build/'mka-audio-profile').resolve()),'--backend',name,'--device',device,'--seconds',str(args.seconds),'--frames',str(frames),'--json',str(path.resolve())],check=True)
    data=json.loads(path.read_text())
    assert not data['failed'] and data['callbacks']>0 and data['frames']>0,data
    assert data['callbacks']==data['timing_samples']+data['dropped_timing_samples'],data
    assert data['wall_ns']>=args.seconds*0.95e9,data
    results.append(data)
assert results,'No compiled Linux backend was exercised'
(args.output/'summary.json').write_text(json.dumps({'platform':platform.platform(),'machine':platform.machine(),'seconds_per_backend':args.seconds,'transport':'private virtual endpoints; not physical end-to-end latency','results':results},indent=2)+'\n')
