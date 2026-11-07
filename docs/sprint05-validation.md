# WASAPI validation

Build and test on a Windows x64 Visual Studio 2022 17.14+ developer shell:

```powershell
cmake --preset windows-wasapi
cmake --build --preset windows-wasapi --parallel 2
ctest --preset windows-wasapi
ctest --test-dir build/windows-wasapi -L hardware --output-on-failure
```

The unit cases use actual Windows SDK COM interfaces with controlled native
implementations, real MTA threads and real event handles. They cover discovery,
UTF-8 IDs, exact formats, shared initialization, render/capture buffers, startup
rollback, in-flight stop, device/service/resource invalidation, xruns, the public
Backend error/status/event contract and 1,000 complete public lifecycle cycles.
The native factory override is private to test builds. A separate library-only
Windows build verifies that the override and GoogleTest are not compiled in.

Hardware cases use the public backend and an actual active endpoint. They skip
explicitly if the runner has no usable audio endpoint/service; a skip is not
hardware validation. Render tests produce silence. Actual unplug/replug endurance
requires a Windows audio device; controlled HRESULT injection is reported
separately from a physical unplug.

The `WasapiRt` cases instrument C++ ordinary/array/nothrow/aligned new/delete around
the complete processing routines and run all six conversions through render/capture. Their
self-test proves that the counter detects allocations and releases. This checks
owned C++ processing code with native-interface fakes, not allocator behavior
inside the Windows audio engine. Processing also avoids STL debug iterators;
configuration, COM, event waits, allocation and release remain outside callbacks.

For memory safety use the `windows-wasapi-asan` configure/build/test presets.
MSVC AddressSanitizer runs the public/stress/fault tests with allocator
interposition excluded. CI verifies the actual compile flags and runs an isolated
heap-overflow witness from RUNNER_TEMP. Windows MSVC does not provide UBSan,
TSan or LeakSanitizer; the Linux core sanitizer jobs and native object/handle
counters provide their respective evidence without claiming those tools on Windows.

The GitHub `Windows C++26 checks` workflow uploads JUnit, CTest logs and ASan
witness output. Each PR carries its own check results; do not infer a physical
hardware success from compilation or from a green job containing SKIP cases.
