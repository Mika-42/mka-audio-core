"""Check that ASIO is isolated from Core and public module consumers."""
import argparse
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("build", type=Path)
parser.add_argument("--library-only", action="store_true")
args = parser.parse_args()
entries = json.loads((args.build / "compile_commands.json").read_text())
assert any(entry["file"].replace("\\", "/").endswith("/windows/asio.cppm") for entry in entries)
for entry in entries:
    filename = entry["file"].replace("\\", "/")
    command = entry["command"].replace("\\", "/").lower()
    if "/src/core/" in filename or filename.endswith("/asio/test_module.cpp"):
        assert "asiosdk/common" not in command, entry
    if args.library_only:
        assert "/test/" not in filename and "googletest" not in filename, entry
        assert "mka_asio_testing" not in command, entry
print("ASIO target is built; SDK paths stay private; library-only constraints verified:", args.library_only)
