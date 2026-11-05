#!/usr/bin/env python3
"""Relocate a production prefix and hide its source/build trees before consumption."""
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[1]);p.add_argument('--work',type=Path,required=True);p.add_argument('--core-only',action='store_true');a,extra=p.parse_known_args()
a.work.mkdir(parents=True,exist_ok=True);root=Path(tempfile.mkdtemp(prefix='relocation-',dir=a.work.resolve()))
source=root/'producer-source';build=root/'producer-build';prefix=root/'prefix-a';relocated=root/'prefix-b';client=root/'client-source';client_build=root/'client-build'
shutil.copytree(a.source.resolve(),source,ignore=shutil.ignore_patterns('.git','build','cmake-build-*','__pycache__'))
shutil.copytree(a.source.resolve()/'test/client',client)
options={name:False for name in ('ALSA','JACK','PIPEWIRE','PULSEAUDIO','WASAPI','COREAUDIO')}
if not a.core_only:
    names=('ALSA','JACK','PIPEWIRE','PULSEAUDIO') if sys.platform.startswith('linux') else ('COREAUDIO',) if sys.platform=='darwin' else ('WASAPI',)
    for name in names:options[name]=True
flags=[f'-DMKA_BUILD_{name}={"ON" if on else "OFF"}' for name,on in options.items()]
extra=[value for value in extra if value!='--']
def run(*args):subprocess.run(args,check=True)
run('cmake','-S',str(source),'-B',str(build),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DMKA_BUILD_TESTS=OFF',*flags,*extra)
run('cmake','--build',str(build),'--parallel','2')
run('cmake','--install',str(build),'--prefix',str(prefix))
prefix.rename(relocated)
source.rename(root/'producer-source-unavailable');build.rename(root/'producer-build-unavailable')
for path in relocated.rglob('*.cmake'):
    text=path.read_text()
    assert str(source) not in text and str(build) not in text, f'Producer path leak: {path}'
components=';'.join(name.lower() for name,on in options.items() if on) or 'core'
run('cmake','-S',str(client),'-B',str(client_build),'-G','Ninja',f'-DCMAKE_PREFIX_PATH={relocated}',f'-DMKA_CLIENT_COMPONENTS={components}',*extra)
run('cmake','--build',str(client_build),'--parallel','2')
run('ctest','--test-dir',str(client_build),'--no-tests=error','--output-on-failure','--output-junit',str(a.work.resolve()/'installed-client-results.xml'))
# Core can be selected on its own even when the package contains native backends.
run('cmake','-S',str(client),'-B',str(root/'core-client'),'-G','Ninja',f'-DCMAKE_PREFIX_PATH={relocated}','-DMKA_CLIENT_COMPONENTS=core',*extra)
run('cmake','--build',str(root/'core-client'),'--parallel','2')
run('ctest','--test-dir',str(root/'core-client'),'--no-tests=error','--output-on-failure')
missing=subprocess.run(['cmake','-S',str(client),'-B',str(root/'unavailable-client'),'-G','Ninja',f'-DCMAKE_PREFIX_PATH={relocated}','-DMKA_CLIENT_COMPONENTS=asio',*extra],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
assert missing.returncode!=0 and 'not compiled in this package' in missing.stdout,missing.stdout
print(f'Relocated package passed; producer source/build unavailable, artifacts in {root}')
