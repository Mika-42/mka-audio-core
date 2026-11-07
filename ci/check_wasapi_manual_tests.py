"""Verify that the two audible tests stay discoverable, disabled and outside unit CI."""
import json
import subprocess
import sys

tests = json.loads(subprocess.check_output(
    ["ctest", "--test-dir", sys.argv[1], "--show-only=json-v1"], text=True
))["tests"]
manual = [test for test in tests if ".WasapiManualTest." in test["name"]]
assert {test["name"].split(".")[-1].removeprefix("DISABLED_") for test in manual} == {
    "TestPlaySineWave", "TestRecordAndPlayback"
}
assert len(manual) == 2
for test in manual:
    properties = {prop["name"]: prop["value"] for prop in test["properties"]}
    assert properties.get("DISABLED") is True, test
    labels = set(properties.get("LABELS", []))
    assert {"integration", "hardware", "manual", "wasapi"} <= labels, test
    assert "unit" not in labels, test
print("Both WASAPI audible tests are discovered and require explicit manual execution.")
