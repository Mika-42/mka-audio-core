# Optional backend and diagnostics studies — lot 9

These opt-in prototypes document decisions and measured evidence. Production Core
API and backend selection keep their existing contract. The study targets are
excluded from installed library exports and are disabled by default.

## Operational platform support

| Platform | Backends | Operational support |
|---|---|---|
| Linux | ALSA, JACK, PipeWire, PulseAudio | **Supported and functional** for all documented Linux backend capabilities. |
| Windows | WASAPI; ASIO/WMME/DirectSound under assessment | **Not yet supported** — operational validation in progress. |
| macOS | CoreAudio | **Not yet supported** — operational validation in progress. |

Build/unit-test evidence on Windows/macOS does not complete their operational
validation. See the [capability matrix](../backend-support.md) for each direction,
format, duplex constraint and the scope of native proofs.

## Results and reproduction

| Fiche | Evidence and recommendation |
|---|---|
| O09-01 | [ASIO SDK callback ABI and licensing](asio.md): defer production adoption pending driver and license decisions. |
| O09-02 | [WMME/DirectSound](windows-legacy.md): native capability proof; defer an implicit fallback. |
| O09-03 | [PipeWire duplex](pipewire-duplex.md): two-stream signal/clock study; paired Core callback/clock identity remain open. |
| O09-04 | [PulseAudio duplex](pulseaudio-duplex.md): record/playback signal proof; clock/pairing and RT limitations remain explicit. |
| O09-05 | [Configuration/format fuzzing](format-fuzzing.md): two sanitizer-backed libFuzzer targets and regression corpora. |
| O09-06 | [Structured diagnostics](structured-diagnostics.md): bounded control-side snapshot/JSON prototype; no public ErrorDetails API yet. |

Enable `MKA_BUILD_STUDIES=ON` together with the backend SDK options being studied.
The ASIO study additionally needs Windows, `MKA_STUDY_ASIO=ON` and the separately
licensed `MKA_ASIO_SDK_DIR`. Then run `ctest -L study`; Linux native tests should
run through `test/linux/run_virtual_tests.sh`. Actual server/device absence is a
SKIP, and controlled SDK/virtual proofs do not assert physical microphone/speaker
coverage.
