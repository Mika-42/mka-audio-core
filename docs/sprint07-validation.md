# Sprint 07 — portability and performance

## Availability and external module clients

`knownBackends` contains stable enum identifiers; `supportedBackends` and
`isBackendCompiled()` reflect the producer's CMake switches. Core-only builds have
an empty compiled list, and ASIO/WMME/DirectSound remain unimplemented identifiers.
Neither enumeration guarantees a service, physical endpoint or permission.

`ci/check_client.py` builds independent core/native clients using only public
imports. It runs on Linux, MSVC Windows and upstream LLVM macOS. Clients must use
the same compiler/toolchain, standard dialect and ABI-compatible options as their
modules; in CMake set `CMAKE_CXX_EXTENSIONS OFF` before creating client targets.

## CPU, timing and endurance

Enable `MKA_BUILD_DIAGNOSTICS=ON` to build `mka-audio-profile`. It is an opt-in tool,
not production callback instrumentation or a change to the portable Backend ABI.

```sh
./build/audit/mka-audio-profile --backend alsa --device null --seconds 2 --frames 512
bash test/linux/run_virtual_tests.sh python3 ci/run_audio_audit.py \
  --build build/audit --seconds 60 --output build/audit/results
```

The profiler reserves 100,000 records before start; the callback only samples a
monotonic clock, writes silence and publishes bounded records/counters. Metrics,
sorting, JSON and process CPU queries run outside the callback. CPU is process CPU
as a percentage of one core, including backend/control work. The service window
excludes the observer's final record publication. Clock reads and recording add
measurement overhead; results are observations, not portable latency limits.

Jitter is the absolute difference between successive callback-entry intervals
and the preceding callback's `frames / rate` duration, so changing frame counts
are respected. The report includes sample count and dropped records: saturated
storage describes a prefix of the run, not a fabricated full-run distribution.
Requested period, open/start/stop durations and callback service time do **not**
measure physical capture-to-speaker latency. ALSA null is unpaced and can consume
one CPU core; virtual server measurements are not hardware performance claims.

The CI runs 60 seconds per isolated Linux backend and repeated real JACK server
removal/return. This validates bounded shutdown/reopen and callback lifetime under
native service loss, not a physical USB unplug or an hours-long stability guarantee.
For longer runs choose up to 86,400 seconds with explicit devices and permissions.

ALSA tries a bounded, deduplicated list of SCHED_FIFO priorities and reports Yes
only after reading back the actual policy/priority. Permission denial preserves
a functioning non-RT stream. Tests cover denied privileges, low-priority grants,
invalid ranges and false readback; normal native tests do not require root/rtprio.

## Configuration fuzzing

The deterministic corpus covers zero, signed/unsigned boundaries and transport
overflow. The libFuzzer target exercises actual public Core validation before a
resource hook, uses independent wide-integer safety checks and verifies retry
and state rollback. It does not fuzz OS drivers, native PCM memory or DSP.

```sh
cmake --preset asan -B build/fuzz -DMKA_BUILD_TESTS=OFF -DMKA_BUILD_FUZZERS=ON
cmake --build build/fuzz --parallel 2
python3 ci/run_config_fuzz.py --binary build/fuzz/mka-audio-config-fuzz \
  --work build/fuzz/corpus --runs 100000
```

## Installable CMake package

Build production artifacts with the selected native backends and tests disabled:

```sh
cmake --preset core -B build/package -DCMAKE_BUILD_TYPE=Release -DMKA_BUILD_TESTS=OFF
cmake --build build/package --parallel 2
cmake --install build/package --prefix /tmp/mka-prefix
```

A separate client uses:

```cmake
cmake_minimum_required(VERSION 4.0)
project(MyAudioClient LANGUAGES CXX)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(mkaAudioCore CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE mka::core)
```

Configure that client with the same build configuration/runtime as the installed
archives (particularly MSVC's Release `/MD` versus Debug `/MDd`):

```sh
cmake -S my-client -B my-client-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/tmp/mka-prefix
cmake --build my-client-build
```

Add `COMPONENTS alsa` (or jack, pipewire, pulseaudio, wasapi, coreaudio) and link
`mka::alsa` to consume that compiled backend. Core is always loaded; SDK dependencies
are resolved only for requested native components. An uncompiled component fails
configuration explicitly. `mkaAudioCore_AVAILABLE_BACKENDS` describes the package.

The package exports module sources and compiler requirements, not portable BMIs.
CMake rebuilds consumer BMIs from installed sources; the binary archives still
require a matching C++ compiler/ABI, standard library and native SDK. Private
headers needed by global module fragments are installed alongside module sources.
No test hooks/GoogleTest are included in the production install validation.

`ci/check_install.py` copies an isolated production source, installs it, moves the
prefix and hides both producer source and build directories before compiling and
running an independent client. Export metadata is checked for producer paths.
It also tests core-only component selection and rejection of an unavailable stub.
