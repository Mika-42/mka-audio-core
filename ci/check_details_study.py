#!/usr/bin/env python3
import argparse
import json
import subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);p.add_argument('--output',type=Path);a=p.parse_args()
text=subprocess.check_output([str(a.binary.resolve())],text=True)
records=[json.loads(line) for line in text.splitlines()];assert len(records)==2
snapshot,measurement=records
assert snapshot['schema_version']==1 and snapshot['error']['name']=='EndpointUnavailable'
assert bytes.fromhex(snapshot['request']['id'])==b'probe:\xc3\xa9\x00\xff'
assert snapshot['request']['id_encoding']=='hex' and int(snapshot['request']['id_bytes'])==10
assert snapshot['native']=={'domain':'Unknown','code':None}
assert snapshot['status']=={'xruns':'0','failed':False,'realtime':'Unknown'}
assert measurement['kind']=='control_measurement' and measurement['samples']==1000
assert measurement['total_ns']>=0 and 0<measurement['record_bytes']<10240
if a.output:a.output.write_text(text)
print(json.dumps(measurement))
