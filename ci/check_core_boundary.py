#!/usr/bin/env python3
"""Check actual Core build membership and dependency directives, not comments."""
import argparse,json,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[1]);p.add_argument('--build',type=Path,required=True);a=p.parse_args();root=a.source.resolve()
commands=json.loads((a.build/'compile_commands.json').read_text());core=[]
for entry in commands:
    command=entry['command'].replace('\\','/')
    if 'CMakeFiles/mka-core-audio.dir/' not in command:continue
    file=Path(entry['file']).resolve();assert file.is_relative_to(root/'src/core'),file
    core.append(file)
    for dependency in re.findall(r'^\s*(?:#\s*include\s*[<"]([^>"]+)|(?:export\s+)?import\s+([^;]+))',file.read_text(),re.M):
        value=' '.join(dependency).lower()
        assert not any(name in value for name in ('examples/','test/','dsp','graph')),value
assert core,'No production Core translation unit checked'
print(f'Core/DSP boundary: {len(set(core))} Core units checked')
