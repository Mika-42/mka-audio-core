#!/usr/bin/env python3
"""Prepare and consume candidate binary/source archives; never create a tag/release."""
import argparse
import hashlib
import json
import platform
import shutil
import subprocess
import tarfile
import zipfile
from pathlib import Path

p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[1]);p.add_argument('--work',type=Path,required=True);a,extra=p.parse_known_args()
source=a.source.resolve();work=a.work.resolve();work.mkdir(parents=True,exist_ok=True)
extra=[v for v in extra if v!='--']
def run(*command,**kwargs):return subprocess.run(command,check=True,**kwargs)
def output(*command):return subprocess.check_output(command,text=True).strip()
run('git','-C',str(source),'diff','--quiet','HEAD')
sha=output('git','-C',str(source),'rev-parse','HEAD')
build=work/'producer';artifacts=work/'artifacts';artifacts.mkdir(exist_ok=True)
options={name:False for name in ('ALSA','JACK','PIPEWIRE','PULSEAUDIO','WASAPI','COREAUDIO')}
selected=('ALSA','JACK','PIPEWIRE','PULSEAUDIO') if platform.system()=='Linux' else ('COREAUDIO',) if platform.system()=='Darwin' else ('WASAPI',)
for name in selected:options[name]=True
flags=[f'-DMKA_BUILD_{name}={"ON" if enabled else "OFF"}' for name,enabled in options.items()]
run('cmake','-S',str(source),'-B',str(build),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DMKA_BUILD_TESTS=OFF','-DMKA_BUILD_EXAMPLES=ON','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',*flags,*extra)
run('cmake','--build',str(build),'--parallel','2')
commands=json.loads((build/'compile_commands.json').read_text())
assert all('MKA_WASAPI_TESTING' not in c['command'] and 'MKA_COREAUDIO_TESTING' not in c['command'] and 'googletest' not in c['file'] for c in commands)
metadata=json.loads((build/'release-metadata.json').read_text());assert metadata['stage']=='candidate' and metadata['version']=='1.0.0',metadata
suffix='.exe' if platform.system()=='Windows' else ''
run(str(build/('mka-dsp-example'+suffix)))
client_source=work/'client-source'
if not client_source.exists():shutil.copytree(source/'test/client',client_source)
components=';'.join(name.lower() for name in selected)
packages=[]
def extract(archive,destination):
    destination.mkdir(exist_ok=True)
    if archive.suffix=='.zip':
        with zipfile.ZipFile(archive) as container:
            for name in container.namelist():assert (destination/name).resolve().is_relative_to(destination.resolve()),name
            container.extractall(destination)
            # Python's ZIP extractor does not restore Unix executable modes.
            for member in container.infolist():
                permissions=(member.external_attr>>16)&0o777
                if member.create_system==3 and permissions:(destination/member.filename).chmod(permissions)
    else:
        with tarfile.open(archive) as container:
            for member in container.getmembers():assert (destination/member.name).resolve().is_relative_to(destination.resolve()),member.name
            container.extractall(destination,filter='data')
for generator in ('TGZ','ZIP'):
    run('cpack','-G',generator,'--config',str(build/'CPackConfig.cmake'),'-B',str(artifacts))
    basename=f"mka-audio-core-{metadata['version']}-candidate-{metadata['system']}-{metadata['architecture']}"
    archive=artifacts/(basename+('.zip' if generator=='ZIP' else '.tar.gz'));assert archive.is_file(),archive
    unpacked=work/f'unpacked-{generator}';extract(archive,unpacked)
    configs=list(unpacked.rglob('mkaAudioCoreConfig.cmake'));assert len(configs)==1
    for path in unpacked.rglob('*.cmake'):
        text=path.read_text().replace('\\','/')
        assert source.as_posix() not in text and build.as_posix() not in text,path
    manifests=list(unpacked.rglob('release-metadata.json'));assert len(manifests)==1
    assert json.loads(manifests[0].read_text())==metadata
    demos=list(unpacked.rglob('mka-dsp-example'+suffix));assert len(demos)==1;run(str(demos[0]))
    client_build=work/f'client-{generator}'
    run('cmake','-S',str(client_source),'-B',str(client_build),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DMKA_CLIENT_VERSION=1.0',f'-DmkaAudioCore_DIR={configs[0].parent}',f'-DMKA_CLIENT_COMPONENTS={components}',*extra)
    run('cmake','--build',str(client_build),'--parallel','2')
    run('ctest','--test-dir',str(client_build),'--no-tests=error','--output-on-failure','--output-junit',str(artifacts/f'junit-package-{generator}.xml'))
    packages.append(archive)
source_archive=artifacts/f"mka-audio-core-{metadata['version']}-source.tar.gz"
run('git','-C',str(source),'archive','--format=tar.gz',f"--prefix=mka-audio-core-{metadata['version']}-source/",'-o',str(source_archive),'HEAD')
source_unpacked=work/'source-unpacked';extract(source_archive,source_unpacked)
source_roots=[path.parent for path in source_unpacked.glob('*/CMakeLists.txt')];assert len(source_roots)==1
source_build=work/'source-smoke'
run('cmake','-S',str(source_roots[0]),'-B',str(source_build),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DMKA_BUILD_TESTS=OFF','-DMKA_BUILD_EXAMPLES=ON',*[f'-DMKA_BUILD_{name}=OFF' for name in options],*extra)
run('cmake','--build',str(source_build),'--parallel','2');run(str(source_build/('mka-dsp-example'+suffix)))
packages.append(source_archive)
manifest={**metadata,'git_sha':sha,'published':False,'artifacts':[{'name':path.name,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'bytes':path.stat().st_size} for path in packages]}
(artifacts/'release-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(artifacts/'SHA256SUMS').write_text(''.join(f"{entry['sha256']}  {entry['name']}\n" for entry in manifest['artifacts']))
print(json.dumps(manifest,indent=2))
