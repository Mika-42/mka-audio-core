#!/usr/bin/env python3
"""Build a separate CMake client; producer tests and SDK headers stay private."""
import argparse
import subprocess
import sys
from pathlib import Path

p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[1]);p.add_argument('--work',type=Path,required=True);p.add_argument('--core-only',action='store_true');args,extra=p.parse_known_args()
source=args.source.resolve();work=args.work.resolve()
options={name:False for name in ('ALSA','JACK','PIPEWIRE','PULSEAUDIO','WASAPI','COREAUDIO')}
if not args.core_only:
    if sys.platform.startswith('linux'):
        for name in ('ALSA','JACK','PIPEWIRE','PULSEAUDIO'):options[name]=True
    elif sys.platform=='darwin':options['COREAUDIO']=True
    elif sys.platform=='win32':options['WASAPI']=True
command=['cmake','-S',str(source/'test/client'),'-B',str(work),'-G','Ninja',f'-DMKA_SOURCE_DIR={source}']
command += [f'-DMKA_BUILD_{name}={"ON" if on else "OFF"}' for name,on in options.items()]
# A local toolchain may supply SDK roots without changing the client sources.
command += [value for value in extra if value!='--']
subprocess.run(command,check=True)
subprocess.run(['cmake','--build',str(work),'--parallel','2'],check=True)
subprocess.run(['ctest','--test-dir',str(work),'--no-tests=error','--output-on-failure','--output-junit',str(work/'client-results.xml')],check=True)
