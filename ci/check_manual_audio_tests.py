"""Verify compiled audible tests require explicit opt-in and never join unit CI."""
import json
import subprocess
import sys

build, backend = sys.argv[1:]
suite = {"asio": "AsioManualTest", "directsound": "DirectSoundManualTest", "wmme": "WmmeManualTest"}[backend]
tests = json.loads(subprocess.check_output(["ctest", "--test-dir", build, "--show-only=json-v1"], text=True))["tests"]
manual = [test for test in tests if f".{suite}." in test["name"]]
assert len(manual) == 2
assert {test["name"].split(".")[-1].removeprefix("DISABLED_") for test in manual} == {
    "TestPlaySineWave", "TestRecordAndPlayback"
}
for test in manual:
    properties = {prop["name"]: prop["value"] for prop in test["properties"]}
    assert properties.get("DISABLED") is True, test
    labels = set(properties.get("LABELS", []))
    assert {"integration", "hardware", "manual", backend} <= labels, test
    assert "unit" not in labels, test
print(f"Both {backend} audible tests are compiled, discovered, disabled and separate from unit CI.")
