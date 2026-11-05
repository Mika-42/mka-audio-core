# Sprint 06 — CoreAudio macOS

## Build and tests

Install upstream LLVM, Ninja and CMake 4+ on macOS (`brew install llvm ninja cmake`).
AppleClang does not provide CMake's named-module dependency scanner; the configure
check explains the required compiler instead of silently dropping C++ modules.

```sh
export CXX="$(brew --prefix llvm)/bin/clang++"
cmake --preset macos-coreaudio
cmake --build --preset macos-coreaudio --parallel 2
ctest --preset macos-coreaudio
ctest --test-dir build/macos-coreaudio -L hardware --output-on-failure
```

`macos-core` tests the core without native audio dependencies. The
`macos-coreaudio-asan` preset separately enables AddressSanitizer and UBSan and
excludes global allocator interposition. LeakSanitizer is not available on the
macOS runtime; no leak-detection claim is made for this preset.

The macOS workflow runs these configurations, publishes JUnit and CTest logs,
and builds the production library without GoogleTest or injection hooks. Unit
fault injection uses the real SDK types and callback ABI on macOS, not a simulated
Apple SDK compiled on Linux. The hardware group explicitly skips unavailable
endpoints and capture permission; these skips are not physical audio validation.

## Supported transport

Import `mka.audio.backend.coreaudio` and instantiate `mka::audio::core::CoreAudio`.
The formerly empty `coreaudio` module remains a compatibility import. No SDK type
is added to the portable API.

Endpoints use stable CoreAudio device UID strings. An empty UID selects the
system default input or output; duplex uses both directions of one output device,
not an implicit aggregate of independently clocked devices. Capture is subject to
macOS microphone permission for the host application/terminal. Explicitly enable
hardware capture testing with `MKA_TEST_COREAUDIO_INPUT_UID`; the CI never requests
microphone permission implicitly.

The HAL IOProc supports native packed Float32 virtual streams, interleaved or
planar and multiple streams, with exact native channel counts. Other requested
formats are rejected with FormatNotSupported. There is no hidden resampling,
integer conversion or aggregate-device creation. Rates and frame sizes are set
only when necessary, read back exactly with bounded waits and revalidated before
start. Discovery capabilities are hints, not the Cartesian product of valid opens.

Changing device rate/buffer size affects other clients of that device. The
backend does not take exclusive/hog ownership. On close or rollback, prior values
are restored best-effort if they still equal this session's selected values;
changes from another client are not overwritten. A host needing isolation should
manage a dedicated device and its hardware settings.

Callbacks use independent preallocated planar Float32 scratch memory. Native
capture is deinterleaved; output is cleared every cycle, including when no process
function is installed. Invalid, oversized or inconsistent native buffers fail the
stream and leave output silent. There is no allocation, logging, native property
query, mutex or lifecycle operation on the owned audio path.

Native listeners publish atomic invalidation/xrun flags only. Device death,
stream reconfiguration and nominal rate/buffer/clock changes mute the stream and
emit Failed through the existing non-RT Backend event dispatcher. Processor
overload increments xruns. RealtimeState stays Unknown: the HAL owns the thread,
and this backend does not infer its scheduling policy.

Stop disables client callbacks, stops the native device IOProc and waits for
already executing audio/property callbacks before returning. Failed stop/close
can be retried. Destruction performs cleanup again; if the HAL permanently refuses
to unregister an IOProc/listener, an inactive native callback context is retained
rather than freeing memory still referenced by the OS. Normal paths and retryable
fault paths release all registrations and owned buffers.

## Test evidence and scope

The native unit suite covers discovery errors and malformed properties, absent
UIDs, directions, exact rate/buffer readbacks, partial registration rollback,
interleaved/planar capture and render, variable frame counts, null/partial/oversized
buffers, silent output, failed start/stop/close retries, quiescent stop, invalidation,
overload, reopen and 1,000 complete cycles. SDK function injection is compiled out
of production. The RT probe includes an allocation witness and counts C++
new/delete operations across 64 callbacks and native notifications. It does not
claim to instrument internal HAL/driver allocations or all libc allocation APIs.

Physical render/capture and unplug require an actual Mac and permissions. The
hardware smoke test proves callback progress and post-stop quiescence when an
endpoint is available; it does not prove speaker sound or microphone signal energy.
