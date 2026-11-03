#!/usr/bin/env python3
"""Run the real core CTest suite and publish LLVM source coverage artifacts."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def generate(args):
    build = args.build_dir.resolve()
    source = args.source_dir.resolve()
    binary = args.binary.resolve()
    cache = build / "CMakeCache.txt"
    if not cache.exists() or "MKA_ENABLE_COVERAGE:BOOL=ON" not in cache.read_text():
        raise RuntimeError("Configure with --preset coverage (MKA_ENABLE_COVERAGE=ON) first")
    if not binary.is_file():
        raise RuntimeError(f"Build the core test binary first: {binary}")
    sources = sorted((source / "src" / "core").glob("*.cppm"))
    if not sources:
        raise RuntimeError("No core module sources found")

    report = build / "coverage"
    report.mkdir(exist_ok=True)
    # Never reuse profiles or reports from an earlier, possibly successful run.
    for name in ("core.profdata", "report.txt", "coverage.json", "lcov.info", "junit.xml"):
        (report / name).unlink(missing_ok=True)
    if (report / "html").exists():
        shutil.rmtree(report / "html")
    with tempfile.TemporaryDirectory(prefix="profiles-", dir=report) as directory:
        env = {**os.environ, "LLVM_PROFILE_FILE": str(Path(directory) / "%p-%m.profraw")}
        test = subprocess.run([
            args.ctest, "--test-dir", str(build), "-L", "^core$", "--no-tests=error",
            "--output-on-failure", "--parallel", "2", "--output-junit", str(report / "junit.xml")
        ], env=env, check=False)
        profiles = sorted(Path(directory).glob("*.profraw"))
        if not profiles:
            raise RuntimeError(f"CTest exited {test.returncode}; no LLVM profiles were produced")
        subprocess.run([args.profdata, "merge", "-sparse", *map(str, profiles),
                        "-o", str(report / "core.profdata")], check=True)

    common = [str(binary), f"-instr-profile={report / 'core.profdata'}", *map(str, sources)]
    with (report / "report.txt").open("w") as stream:
        subprocess.run([args.cov, "report", *common], stdout=stream, check=True)
    subprocess.run([args.cov, "show", *common, "-format=html",
                    f"-output-dir={report / 'html'}"], check=True)
    with (report / "lcov.info").open("w") as stream:
        subprocess.run([args.cov, "export", *common, "-format=lcov"], stdout=stream, check=True)
    with (report / "coverage.json").open("w") as stream:
        subprocess.run([args.cov, "export", *common, "-summary-only"], stdout=stream, check=True)
    data = json.loads((report / "coverage.json").read_text())
    files = data["data"][0]["files"]
    allowed = {str(path) for path in sources}
    if not files or any(item["filename"] not in allowed for item in files):
        raise RuntimeError("Coverage must contain only core module sources")
    # Some modules only declare types and have no executable regions. Report
    # them explicitly instead of pretending their lack of counters is 100%.
    measured = {item["filename"] for item in files}
    unmeasured = sorted(allowed - measured)
    with (report / "report.txt").open("a") as stream:
        if unmeasured:
            stream.write("\nModules with no executable coverage records:\n")
            stream.writelines(f"  {Path(name).relative_to(source)}\n" for name in unmeasured)
    print((report / "report.txt").read_text(), flush=True)
    print(f"Coverage artifacts: {report}", flush=True)
    # Failed tests still produce useful diagnostics but must fail this target.
    return test.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--source-dir", required=True, type=Path)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--ctest", required=True)
    parser.add_argument("--profdata", required=True)
    parser.add_argument("--cov", required=True)
    try:
        return generate(parser.parse_args())
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Coverage failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
