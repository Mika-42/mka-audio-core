#!/usr/bin/env python3
"""Fetch the separately licensed, pinned SDK for an opt-in ABI assessment only."""
import argparse
import hashlib
import io
import urllib.request
import zipfile
from pathlib import Path

URL='https://download.steinberg.net/sdk_downloads/ASIO-SDK_2.3.4_2025-10-15.zip'
SHA256='d5ebf0c20dd2c5f43771fd0c1418f4b361bf52434ee670097cfa6b3a335e2eca'
p=argparse.ArgumentParser();p.add_argument('--destination',type=Path,required=True);a=p.parse_args()
with urllib.request.urlopen(URL,timeout=45) as response:data=response.read()
assert hashlib.sha256(data).hexdigest()==SHA256,'Unexpected SDK archive; review provenance before updating the pin'
a.destination.mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile(io.BytesIO(data)) as archive:
    for name in archive.namelist():assert (a.destination/name).resolve().is_relative_to(a.destination.resolve()),name
    archive.extractall(a.destination)
assert (a.destination/'ASIOSDK/common/asio.h').is_file()
print('ASIO SDK 2.3.4 verified:',SHA256)
