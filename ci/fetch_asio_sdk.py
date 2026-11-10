"""Fetch the audited SDK outside the source tree for opt-in Windows validation."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import urllib.request
import zipfile


def fetch_sdk(destination, manifest):
    with urllib.request.urlopen(manifest["url"], timeout=45) as response:
        data = response.read()
    if hashlib.sha256(data).hexdigest() != manifest["archive_sha256"]:
        raise ValueError("Unexpected ASIO SDK archive; review its version and license before changing the pin")
    destination = destination.resolve()
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        for name in archive.namelist():
            if not (destination / name).resolve().is_relative_to(destination):
                raise ValueError("Unsafe SDK archive path")
        destination.mkdir(parents=True, exist_ok=True)
        archive.extractall(destination)
    for name, digest in manifest["files"].items():
        if hashlib.sha256((destination / "ASIOSDK" / name).read_bytes()).hexdigest() != digest:
            raise ValueError("Unexpected ASIO SDK file: " + name)
    print("ASIO SDK", manifest["version"], "verified:", manifest["archive_sha256"])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads((Path(__file__).resolve().parents[1] / "cmake/asio-sdk-2.3.4.json").read_text())
    fetch_sdk(args.destination, manifest)


if __name__ == "__main__":
    main()
