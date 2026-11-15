# Backend / OS support

This matrix describes the implementations in this repository. `AudioBackend` and
`knownBackends` identify known backends; `supportedBackends` / `isBackendCompiled()`
reflect the producer's CMake options. Neither implies an available service,
physical device, permission or a configuration that will open. `open()` revalidates
native capabilities, with explicit `Result` errors and no silent approximation.

| Backend | OS / target | Directions | Accepted transport formats | Buffer meaning | Realtime status |
|---|---|---|---|---|---|
| ALSA | Linux / `mka::alsa` | input, output, linked duplex | Int16, Int24, Int32, Float32, Float64 subject to native PCM | exact period; negotiated whole-period ring | Yes only after SCHED_FIFO readback; No on refusal |
| JACK | Linux / `mka::jack` | input, output, same-client duplex | Float32 | initial server quantum must match; callback frames remain authoritative | server-reported Yes/No |
| PipeWire | Linux / `mka::pipewire` | input or output | Float32 planar (F32P) | latency request; bounded variable callback frames | Unknown |
| PulseAudio | Linux / `mka::pulseaudio` | input or output | Float32 | latency request and scratch chunk capacity | Unknown |
| WASAPI | Windows / `mka::wasapi` | capture or render, shared event-driven mode | Int16, Int24, Int32, Float32, Float64 only when supported exactly | shared engine buffer, variable packets/render availability | MMCSS grant/refusal |
| CoreAudio | macOS / `mka::coreaudio` | input, output, same-device/clock duplex | packed native virtual Float32, planar or interleaved | native frame size, checked exactly; bounded callback frames | Unknown (HAL owns the thread) |
| ASIO | Windows identifier only | unimplemented | none | none | none |
| WMME | Windows identifier only | unimplemented | none | none | none |
| DirectSound | Windows identifier only | unimplemented | none | none | none |

All process callbacks expose borrowed Float32 planar views regardless of transport
format. Inputs are immutable; output starts silent. Native conversion/layout work
belongs to the backend; graphs, gain/effects and fixed DSP blocks belong to the
application. Configuration/sample counts use uint32_t; xrun counts use uint64_t.

ALSA duplex requires linked native PCMs; JACK connects input/output ports of the
same peer. CoreAudio duplex does not create an aggregate device. WASAPI, PipeWire
and PulseAudio reject duplex with ConfigurationFailed; two independent streams
are not a synchronized duplex stream. WASAPI exclusive mode is assessed in
[its separate note](wasapi-exclusive-assessment.md), and is not enabled implicitly.

## Identity and runtime behavior

ALSA accepts PCM identifiers, JACK selects a peer client, PipeWire/PulseAudio use
their node/device names, WASAPI uses UTF-8 endpoint IDs and CoreAudio uses device
UIDs. Empty IDs select defaults only where the backend documents that behavior;
use discovered IDs for explicit selection. Numeric ALSA card indices can change.

Use `stop(); close(); open(newConfig); start()` on the control thread to switch
endpoints. A failed stop keeps Running and must be retried; a failed open remains
Closed and permits another configuration. Keep callback user data alive through
all cleanup, including failures. No control calls are allowed from the audio
callback or the event dispatcher. EndpointConfig stays immutable while open.

`Status::xruns` is cumulative since start, `failed` is terminal for that run, and
`realtime` distinguishes Unknown/No/Yes. Native overload/underrun/overflow paths
publish atomics; the Core dispatcher coalesces XRun events and emits Failed once
outside the audio thread. Recover from Failed through stop/close/reopen/start.
An Unknown scheduling state is not a claim that realtime was denied.

## Evidence and prerequisites

| OS | Build / automated proof | Physical coverage limit |
|---|---|---|
| Linux | Clang 19 + libstdc++ 14; 16 option combinations; isolated ALSA null / JACK dummy / PipeWire / PulseAudio, native fault/PCM tests, soak and server-loss recovery | virtual endpoints do not prove speaker/microphone signal or USB unplug |
| Windows | MSVC 17.14+, CMake `/std:c++latest` mode, SDK COM/audio fault injection and native tests; AddressSanitizer | hardware tests explicitly skip without an audio service/endpoint |
| macOS | upstream LLVM, CMake module scanner, Apple SDK / native HAL callback ABI and fault tests; ASan/UBSan | capture requires explicit UID and microphone permission; physical unplug is not simulated as a hardware PASS |

Core-only clients require CMake 4+, Ninja and a supported named-module compiler.
AppleClang currently lacks CMake's required dependency scanner; use upstream LLVM.
MSVC's latest language mode is exposed by CMake as cxx_std_23; GCC/Clang builds
require cxx_std_26. No claim of complete future C++26 feature coverage is made.

See [Linux validation](sprint03-validation.md), [WASAPI validation](sprint05-validation.md),
[CoreAudio validation](sprint06-validation.md) and [portability/performance evidence](sprint07-validation.md).
