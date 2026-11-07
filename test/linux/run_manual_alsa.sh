#!/usr/bin/env bash
# Explicitly build/run one audible ALSA test without editing DISABLED_ in source.
set -euo pipefail
if [[ $# -lt 1 || $# -gt 2 || ( $1 != sine && $1 != record ) ]]; then
    echo "Usage: bash test/linux/run_manual_alsa.sh <sine|record> [build-directory]" >&2
    exit 2
fi
build_dir=${2:-cmake-build-debug}
if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
    echo "No configured CMake build in $build_dir; configure with MKA_BUILD_ALSA=ON first." >&2
    exit 2
fi
cmake --build "$build_dir" --target mka-audio-alsa-tests
if [[ $1 == sine ]]; then test_name=TestPlaySineWave; else test_name=TestRecordAndPlayback; fi
exec "$build_dir/mka-audio-alsa-tests" --gtest_filter="*.$test_name:*DISABLED_$test_name" --gtest_also_run_disabled_tests
