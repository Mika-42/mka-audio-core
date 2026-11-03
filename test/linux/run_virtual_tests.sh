#!/usr/bin/env bash
# Run a test command against private virtual servers.
set -euo pipefail
if [ "$#" = 0 ]; then echo "Usage: bash test/linux/run_virtual_tests.sh <test command> [args...]" >&2; exit 2; fi
fixture_dir=$(cd -- "$(dirname -- "$0")/fixtures" && pwd)
for program in pulseaudio pactl jackd pipewire pw-cli pw-cat wireplumber python3; do
    if ! command -v "$program" >/dev/null; then echo "Missing test dependency: $program" >&2; exit 1; fi
done
run_dir=$(mktemp -d "${TMPDIR:-/tmp}/mka-audio-tests.XXXXXX")
mkdir -p "$run_dir/runtime" "$run_dir/pulse-state" "$run_dir/pulse-runtime"
chmod 700 "$run_dir/runtime" "$run_dir/pulse-runtime"
export XDG_RUNTIME_DIR=$run_dir/runtime
export PIPEWIRE_RUNTIME_DIR=$XDG_RUNTIME_DIR
export PIPEWIRE_CONFIG_DIR=$fixture_dir
export PULSE_RUNTIME_PATH=$run_dir/pulse-runtime
export PULSE_STATE_PATH=$run_dir/pulse-state
export PULSE_SERVER=unix:$run_dir/native
export JACK_DEFAULT_SERVER=mka-test-$$
export MKA_TEST_ALSA_DEVICE=null
export MKA_TEST_JACK_DEVICE_ID=system
export MKA_TEST_PIPEWIRE_DEVICE_ID=mka_test_sink
export MKA_TEST_PULSEAUDIO_DEVICE_ID=mka_test_sink
pids=()
cleanup() {
    if [ "${#pids[@]}" -gt 0 ]; then kill "${pids[@]}" 2>/dev/null || true; fi
    wait || true
    if [ "${MKA_KEEP_TEST_LOGS:-0}" = 1 ]; then echo "Audio fixture logs: $run_dir" >&2; else rm -rf -- "$run_dir"; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
cat > "$run_dir/pulse.pa" <<EOF
load-module module-native-protocol-unix socket=$run_dir/native auth-anonymous=1
load-module module-null-sink sink_name=mka_test_sink rate=48000 channels=2
load-module module-null-source source_name=mka_test_source rate=48000 channels=2
set-default-sink mka_test_sink
set-default-source mka_test_source
EOF
pulseaudio -n -F "$run_dir/pulse.pa" --daemonize=no --use-pid-file=no --exit-idle-time=-1 > "$run_dir/pulse.log" 2>&1 &
pids+=($!)
jackd --name "$JACK_DEFAULT_SERVER" --no-realtime -d dummy -r 48000 -p 256 > "$run_dir/jack.log" 2>&1 &
pids+=($!)
pipewire -c "$fixture_dir/pipewire.conf" > "$run_dir/pipewire.log" 2>&1 &
pids+=($!)
wireplumber > "$run_dir/wireplumber.log" 2>&1 &
pids+=($!)
ready=0
for i in $(seq 1 100); do
    if pactl info >/dev/null 2>&1 && pw-cli info 0 >/dev/null 2>&1 && python3 "$fixture_dir/jack_ready.py" > "$run_dir/jack-ready.log" 2>&1; then ready=1; break; fi
    sleep 0.1
done
if [ "$ready" != 1 ]; then export MKA_KEEP_TEST_LOGS=1; echo 'Virtual audio servers did not become ready.' >&2; exit 1; fi
# Read stdin as raw data: a /dev/zero filename can be treated as a zero-length file.
pw-cat --playback --raw --rate 48000 --channels 2 --format f32 --target 0 --properties '{ media.class = Audio/Source node.name = mka_test_source node.virtual = true node.always-process = true }' - < /dev/zero > "$run_dir/source.log" 2>&1 &
pids+=($!)
source_ready=0
for i in $(seq 1 50); do
    if pw-cli ls Node | python3 -c 'import sys; sys.exit(0 if "mka_test_source" in sys.stdin.read() else 1)'; then source_ready=1; break; fi
    sleep 0.1
done
if [ "$source_ready" != 1 ]; then export MKA_KEEP_TEST_LOGS=1; echo 'Virtual capture source did not appear.' >&2; exit 1; fi
sleep 0.3
"$@"
