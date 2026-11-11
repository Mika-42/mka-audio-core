"""Check DirectSound ON/OFF and the independent production build."""
import argparse
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("build", type=Path)
parser.add_argument("--disabled", action="store_true")
parser.add_argument("--library-only", action="store_true")
args = parser.parse_args()
entries = json.loads((args.build / "compile_commands.json").read_text())
files = [entry["file"].replace("\\", "/") for entry in entries]
present = any(name.endswith("/windows/ds.cppm") for name in files)
assert present != args.disabled, ("DirectSound presence", present, args.disabled)
if args.library_only:
    for entry, name in zip(entries, files):
        assert "/test/" not in name and "googletest" not in name, entry
        assert "MKA_DIRECTSOUND_TESTING" not in entry["command"], entry
        if "/src/backend/" in name:
            assert name.endswith(("/windows/ds.cppm", "/windows/ds_compat.cppm")), entry
print("DirectSound build isolation verified; enabled:", present, "production:", args.library_only)
