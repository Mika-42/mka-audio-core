# Integrating application DSP

The DSP node lives in `examples/dsp/gain.hpp`, outside Core. Build the runnable,
SDK-independent teaching example with `MKA_BUILD_EXAMPLES=ON`; `mka-dsp-example`
uses an explicitly offline transport and requires no sound device. Its worker
supplies variable callback sizes and verifies the processed samples.

## Native platform support

| Platform | Backends | Operational support |
|---|---|---|
| Linux | ALSA, JACK, PipeWire, PulseAudio | **Supported and functional** — all four Linux backends are covered. |
| Windows | WASAPI; ASIO/WMME/DirectSound under assessment | **Not yet supported** — operational validation is in progress. |
| macOS | CoreAudio | **Not yet supported** — operational validation is in progress. |

The offline example can run independently of native device support. For native
audio, use the [documented formats and directions](../docs/backend-support.md).
Windows/macOS build and unit-test coverage exists; complete operational validation
remains pending.

A native application's binding uses the same public API:

```cpp
mka_example::Gain dsp;  // outlives backend, including failed stop/cleanup paths
YourChosenBackend backend;
backend.setProcessFunction(+[](void* user, const mka::audio::core::AudioProcessContext& context) noexcept {
    static_cast<mka_example::Gain*>(user)->process(context);
}, &dsp);
// open(exact native config), start(), then stop() and close() on the control thread.
```

Check every returned `Result`; the executable shows a complete lifecycle. The node
reads immutable input and writes output directly through the borrowed planar views.
It stores counters, not pointers. Its fixed gain needs no concurrent parameter
updates. Output channels without corresponding inputs remain silent. Do not read,
free or retain borrowed buffers after the callback; DSP graphs, fixed-block adapters
and their preallocated state belong to the application, not Core.

The native endpoint chooses which directions exist. WASAPI, PipeWire and PulseAudio
currently reject a duplex request; two independent streams do not imply a common
clock. The offline example is not a claim that every native endpoint supports duplex.
